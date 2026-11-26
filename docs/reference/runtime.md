# Runtime reference

The intended runtime path is the shared N64ModernRuntime implementation:
translated guest threads and RDRAM are owned by librecomp/ultramodern, with
RT64 receiving graphics work and the host owning SDL presentation.

## Developer checkpoint boundary

`func_80001D88` owns the render/game dispatcher loop. At `80001D90` it reloads
the u32 callback at `8007D848`, then calls it indirectly at `80001D98`. The
save-state hook runs before that reload on every loop iteration. Recognized
callbacks are `8004E950` (menu) and `8000C560` (race); loaders are excluded.
`800923E4` is the u32 scene ID, and `80092360..800923FF` is the race setup.
The hook copies the low 8 MiB allocation without changing its word-swapped
layout. See [save-state workflow and limits](../debugging.md#development-save-states).

Race identity checks compare configuration offsets `00..27` (participant
counts/model grid) and `70..87` (course/layout/mirror/variant/laps/mode), as
constructed by `80068AA8`. The full `0xA0`-byte structure is live race state:
for example, `8004CC00/14` stores `+68` during play. Comparing every byte
rejected a measured F8 load three seconds after F7 in the same practice race.
Live fields are restored from the checkpoint without requiring equality.

Pinned ultramodern stores a native `UltraThreadContext*` in each guest
OSThread. Patch 0019 registers live contexts on creation, removes them on
destruction/termination, validates the complete roster and its queue metadata,
and prepares a scratch snapshot. Saves zero those pointers; loads reconnect
them to the current process before publishing RAM. Roster addresses must be
8-byte aligned and the runtime's full OSThread must fit in the snapshot.
No guest scheduling call may occur between preparation and publication.
This is pointer repair, not native stack or scheduler serialization.

All translated calls to the measured `osGetCount` entry at `80071930` route
through `wdc_get_count`. Its host-owned atomic bias initially equals zero.
A successful checkpoint load resets that bias to the difference between the
current native count and the saved guest count, modulo 2^32. This preserves
RAM clock-anchor ages without rebasing the host timer thread or guessing a
list of game clock fields. It does not restore native register-held samples.

## Custom starting grid

The entry to `func_800369E0` applies the settings after the warp hook and
before loading vehicles. The USA config at `80092360` holds u32 participant
counts at offsets `00` and `04`, model IDs at `08..27`, palette IDs at
`28..47`, zero-based starting positions at `48..67`, and mode at `84`.
The loader passes the model and palette words to `func_80031640` at
`80037034..40`. Mode 3 (Arcade Race) assigns positions in reverse slot order
at `80068D24..38`. Mode 2 (Championship) fills them from entrant order at
`80067FE4..80067FF0`.

The dispatcher writes the selected model and palette words for active CPU
slots. Native setup or retry restores the starting grid before each load, so
the loader can apply the latest choices to the live config. The hook leaves
the cached setup at `80099B54` available for retries.

## Input-worker startup ordering

The USA initializer `func_8002CB0C` creates thread 9 (entry `0x8002C500`,
priority 11, OSThread at `0x800B7620`). The ROM starts it at `0x8002CCF4`
before initializing the eight input snapshots at `0x800B97E8`, stride `0x200`.
Each snapshot contains four `0x7C`-byte controller records; `func_8002C494`
and `func_800036CC` set their pad pointers and snapshot back-pointers.

A Windows boot reproduced a preemption before that loop: the worker reached
`func_80002F58` with snapshot `0x800B97E8` and a null pad pointer at offset
`0x74`, then faulted reading the pad's status byte. All eight snapshots were
still uninitialized, and the creator was suspended in `osStartThread`.
Other boots passed this point, making splash-screen failure timing dependent.

`rom-symbols.toml` suppresses the early start instruction and calls the same
OS entry at `0x8002CD20`, after the initialization loop and before the creator's
epilogue. The generator verifies the replaced instruction against the ROM.
The scheduler and input-reading code retain their existing behavior.

`wdc_startup_rom` executes the generated initializer and both real ring
initializers. Its OS-start boundary checks all 32 pad pointers and back-pointers
immediately, modeling a worker that preempts its creator. It checks fresh
initialization and reinitialization, exactly one start per creation, the worker
identity, and preserved stack/callee-saved registers. The original generated
code fails this test before the first worker start completes. Run it with
`ctest --test-dir build -R wdc_startup_rom --output-on-failure` after regeneration.

On 2026-10-23 the regenerated startup was compiled and linked into the Windows
D3D12 player using the existing dependency/build objects. Three fresh
15-second diagnostic runs passed the transition and exited with the expected
diagnostic code 2; a separate run stayed alive for several minutes and rendered
the 3D intro. The focused startup regression, 17 existing CTest entries, nine
Python tests, documentation, repository-hygiene and whitespace checks passed.
This was an incremental player rebuild, not a clean build or full-race test.

## Extended-memory visibility reservation

The enhanced track-section list occupies `0x80FF2880..0x80FF387F` in extended
emulated memory. This is outside the original 8 MiB game memory and below
`0x81000000`, where mod memory and the librecomp heap start. Runtime patch
data is copied from `0x80801000` upward; patch data large enough to reach the
reservation is unsupported. The generated ROM instruction patches in
`rom-symbols.toml` are the only supported writers/readers of that list. See
[distance and detail](visibility.md).

## BOSS graphics task cost

`src/boss_rsp.cpp` runs each BOSS graphics task synchronously on the graphics
worker and passes its RDP ranges to RT64. The generated BOSS code writes RDRAM
only through `wdc_boss_dma_write` (`src/boss_dma.h` is force-included into
`src/aspMain.cpp`). `WDC_BOSS_CAPTURE` records one task, and `wdc_boss_bench`
replays it offline; see [BOSS task capture](../../tools/boss/README.md). OS
event registration lives in `src/boss_events.cpp`, so the replay tool links
the device without the guest OS.

For offline task profiling, live timing methods and historical measurements,
see [Rendering and frame rate](../rendering.md#measuring-frame-time).

### Quick Race car preview reload

The menu's preview loader at `0x8005C198` can overwrite car texture commands
still referenced by the previous BOSS main list. Changing class or car can
make BOSS read a zero command and exit on an invalid microcode jump.

The port tracks submissions at `osSpTaskStartGo` and waits at the preview
loader until BOSS finishes or reaches the EndMainDL wait for SIG0. In the USA
microcode, `0x11AC` marks the main list done in DMEM byte `0x941`, then
`0x1138` polls SP_STATUS. The menu builds the next sublist after the reload
and publishes it at the call to `0x80024094` from `0x8004EBDC`. BOSS can then
read the updated car data and finish the task. [Issue #63](https://github.com/alondero/worlddriverchampionship-recomp/issues/63)

Runtime patches must be listed in `patches/README.md`, applied by the scripts,
and accompanied by a focused reproducer or test. A patch that is useful to a
general upstream project should also be compared with upstream before being
kept locally.
