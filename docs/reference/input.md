# Input integration reference

The shared frontend owns device profiles, binding display and remapping. WDC
owns defaults, SDL-thread sampling, the gameplay snapshot, and the boundary
between settings input and guest input. The player instructions are in
[Controllers](../controllers.md).

## Peer comparison

[Zelda64Recomp's controls adapter](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/src/game/controls.cpp)
combines separate keyboard and controller mappings into N64 button bits and a
normalized stick. Its controller stick receives a deadzone before keyboard
directions are added; the result is clamped to the runtime range.
[Starfox64Recomp's adapter](https://github.com/sonicdcer/Starfox64Recomp/blob/f2996555d73ca480f3dc3639f375c50818847bfc/src/game/controls.cpp)
uses the same structure but scales the stick by 0.65. That scale is game policy
and is not evidence for WDC steering.

[AeroGauge's settings](https://github.com/alondero/aerogauge-recomp/blob/d4408065e88ab089f2c3e934370353e638f06e22/src/ui/aero_frontend_settings.cpp)
use `recompinput` defaults and game-specific action labels, then instantiate
the shared `create_controls_tab()`. This avoids maintaining another binding
editor. [Automobili Lamborghini's input adapter](https://github.com/alondero/automobililamborghini-recomp/blob/8936df4256932b19ce4e36be22b174da4ab8da27/src/ui/lambo_frontend_input.cpp)
also uses frontend profiles, while keeping analog pedals, steering changes
and phone sensors in the port. Those assists require separate game evidence.

## APIs in the pinned dependencies

These links describe WDC's pinned RecompFrontend revision, rather than assuming
that the newest upstream API is available.

- [Input mappings](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/input_mapping.cpp)
  provide per-device defaults, including signed controller half-axes. Set
  WDC defaults before loading profiles: setting defaults after their first use
  throws.
- [Profiles](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/profiles.cpp)
  load and save mappings and evaluate `get_n64_input()`. Loading initializes
  profiles and supplies defaults when the file is absent. Evaluation combines
  keyboard and controller input, applies the joystick deadzone, and clamps
  axes. The host must validate the N64 port: this function itself returns true
  without enforcing WDC's supported port count.
- [The controls tab](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompui/src/config/ui_config_tab_controls.cpp)
  constructs the shared controls page and saves `controls.json` beneath
  `recomp::get_config_path()` when the tab closes.
- [The controls page](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompui/src/config/ui_config_page_controls.cpp)
  reads action names, descriptions and bindings from the input library. Its
  binding buttons and clear/reset actions edit those same profiles.
- [Runtime input callbacks](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/ultramodern/include/ultramodern/input.hpp)
  expose polling, N64 state, rumble, and connected-device/accessory information.
  They do not implement an SDL device manager or binding editor.

## Host ownership requirements

[Frontend polling](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/input_state.cpp)
reads SDL keyboard and controller state. Keep sampling on WDC's SDL main
thread, and publish a coherent host snapshot for the runtime callback. Serialize
profile evaluation with the render-thread binding editor. The frontend's
capture check does not replace WDC's immediate pending-open gate or the
release-before-resuming rule.

[Frontend event handling](https://github.com/N64Recomp/RecompFrontend/blob/b1a1477c6556aeb7ed45defbfb5924f721efebc1/recompinput/src/input_events.cpp)
combines hotplug, binding capture and menu events with fullscreen, launcher,
file-drop and quit behavior. Its public `handle_events()` consumes SDL's event
queue. A port retaining its own event loop must integrate this deliberately;
running both consumers loses events. The pinned remove path erases controller
state without closing the SDL handle, so the host must establish handle
ownership. Device-open failure must not proceed to GUID access.

Controller Pak support remains independent of button mappings. Preserve the
port's accessory callback; adding host rumble does not prove that the guest
Rumble Pak path works.

## WDC adapter and verification

`src/ui/input.cpp` configures defaults before `config::finalize()`, which loads
the native profiles. `src/main.cpp` owns the SDL queue and feeds events through
patch 0017's `process_event()`, then finishes the batch and samples profiles.
Fullscreen, quit and file-drop handling stay with WDC. The Controls tab is
the native editor, with WDC action names; General exposes its deadzone and
background-input and rumble-strength settings without advertising unsupported aiming.

The main thread and presentation callback acquire the same recursive mutex
before accessing native input/profile state. It precedes the frontend's UI
lock. Runtime input callbacks read a single atomic snapshot containing the
N64 button mask and signed normalized axes; they reject ports other than 1
and never call SDL. Opening the menu raises the existing capture gate before
presentation. Resuming requires every mapped gameplay input to return to
neutral, including opposing bindings whose combined stick value is zero.

## Analog driving bridge

`src/game_driving.cpp` adds three USA hooks from `rom-symbols.toml`:

| Boundary | Operand and behavior |
| --- | --- |
| `func_80002F58`, before `800031B4` | `a0` points to a controller ring record. Add accelerate/reverse/brake actions while preserving low menu/button bits. |
| `func_80016DF8`, before `80016EB4` | Replace `a3`'s hardcoded float32 1.0 with normalized throttle for `func_80017530`. |
| `func_80016DF8`, before `80017078` | Scale fresh front/rear brake torque in `f2`/`f0`, retaining front rolling resistance `min(20, full torque)`. |

The game owns eight 0x200-byte ring slots at `800B97E8`; each holds four
0x7C-byte port records. Record `+74` points to `OSContPad` at `800AAAE0 + port*6`.
The bridge validates slot, port and pointer agreement before accessing fields.
Record `+0` is a u32 action mask; `+25` is signed stick Y; `+27` disables driving
actions; `+28` records controller errors. Big-endian guest fields use the
word-swapped MEM helpers. The controller worker only adds high action bits:
`00010000` acceleration, `00020000` reverse below Y=-40, `00040000` braking.
Synthetic throttle/brake are tagged with bits 29/30. Physical A/B's low
translated bits (`0010`/`0020`) take precedence and keep full digital power.
Replay mode (`8007DF94 == 2`) ignores live pedals; replay/demo mask replacements
also clear the tags, so untagged driving stays on the original path.

SDL-thread sampling evaluates positive/negative half-axes through recompinput's
shared axis API. The pinned single-player mode leaves
`players::get_player(0).controller` unassigned; the shared API handles connected
controllers consistently with existing button bindings. The Driving config is
read under the input mutex and values are bounded before normalization. One
lock-free atomic pair per port publishes unitless float32 values in [0,1]; only
port 1 currently receives host input. Guest hooks never call SDL or retain guest
pointers. The pending-open gate immediately masks pedal reads. Sampling also
publishes zero while the overlay is open or waiting for all gameplay inputs,
including enabled pedals below the digital button threshold, to release.
Replace the address bridges with named input/drivetrain APIs when decompiled.

The `wdc_input` CTest target uses SDL's virtual gamepad API with the real
native profiles/event/polling code. It covers hotplug, failed opens, partial
steering and Y direction, face/C/D-pad inputs, capture and release gating,
binding cancellation/remapping, saved-profile reload, a 100% deadzone,
disconnection and absent guest ports. It requires frontend dependencies but
no ROM, physical controller or GPU. This fixture does not establish physical
device compatibility or validate racing feel.

`wdc_input` also covers opt-in pedals, positive/negative half-axes, None,
deadzones/saturation, simultaneous pedals, held-trigger release gating and
detachment. `wdc_frontend_settings` checks Driving defaults, native persistence
and live reset. `wdc_driving` uses synthetic RDRAM for all ring slots/ports,
pointer bounds, disabled/error records, invalid host values, replay and digital
overrides. `wdc_driving_rom` extracts the real input/drivetrain call closure,
including gearbox, steering and torque calculations, with only the port hooks
native. Controlled car parameters and USA ROM constants verify progressive
drive/brake torque, reverse requests and digital overrides. Missing translated
bodies, indirect calls or generated pedal hooks fail extraction. CTest bounds
the ROM fixture to 30 seconds; it is not a physical-controller playthrough.

## Rumble alongside saves

The accessory callback still reports `ControllerPak`. WDC's primary 104-byte
`OSPfs` array starts at `800AAC98` (port 2 is `800AAD00`). File operations and
the motor worker share this array. The separate auxiliary motor-detection
object at `800AAAF8` must fail with `PFS_ERR_DEVICE`, keeping the save/menu
accessory classification truthful. The native adapter accepts only the primary
port-1 object, returns `CONTRFAIL` for absent channels and never changes the
Pak image. This bypasses the runtime's exclusive accessory-kind check.

Correction to the earlier object description: `800AAC98` is not motor-only,
and `800AAAF8` is not the primary save object. The filesystem-init call at
`8003BA24` and ID copy at `8003BA9C` establish the primary object's role; the
motor fallback at `8003BB24` uses the auxiliary object. The addresses did not
change; their earlier interpretation was wrong.

ROM inspection identifies these users of the primary object:

| Reader/caller | Object use |
| --- | --- |
| `func_80038DA4`, `func_80038FEC`, `func_80039318`, `func_8003A5F0`, `func_8003B264` | Pass the primary array to translated file-state, directory, free-space and note I/O routines. Those SDK routines read status, queue/channel, bank and filesystem metadata. |
| `func_8003B984`, call at `8003BA24` | Initializes the primary filesystem object; `8003BA9C` copies its 32-byte ID from `+0C`. The later call at `8003BB24` probes the auxiliary object for a motor. |
| `func_80062060`, call at `80062194` | Copies the primary object's ID at `+0C` into the game's Pak state. |
| `func_8004D854`, `func_8004D450` | Live input-worker callers pass the primary address as an identity token to native motor Init/Start/Stop and consume return codes. They do not transfer object ownership. `func_8004D9B0` creates timer requests without reading an `OSPfs`. |

The ROM's `osMotorInit` stores queue/channel at `+4/+8` (`80073C08`,
`80073C18`), clears status at `+0`, and writes bank `80` at `+65`
(`80073C20`, `80073C28`). These are guest offsets measured from the USA ROM,
also matching the [SDK OSPfs layout](https://github.com/zeldaret/mm/blob/main/include/PR/os_pfs.h).
They are not offsets inferred from the runtime's word-swapped host structure.
The original Start/Stop routines read queue and channel for Joybus access.

The ROM reference fixture independently recompiles that initializer with no
instruction patches. A controlled memory-Pak probe response makes it return
`DEVICE`; the test compares all 104 bytes against the original stores: status
zero, supplied queue/channel and bank `80`, with every other byte preserved.
Thus a physical motor probe really can invalidate the primary filesystem
object. Calling the real `wdc_pfs_init` restores it. This fixture demonstrates
the SDK transition; it does not claim a full original-game recovery trace.

The combined virtual accessory deliberately bypasses that physical probe.
`wdc_motor_init`, `wdc_motor_start` and `wdc_motor_stop` access **no guest
memory**. The primary address is only an identity token; queue is unused and
initialization/motor state lives in host atomics. There is no motor status bit
or bank write. The filesystem owns every byte of the primary object. This
avoids both invalidating live saves and racing a file routine's status/bank
updates. The original translated SDK Start/Stop field readers are replaced
by these adapters; game effect callers use their return codes, not their
queue/channel fields. The ROM test asserts all 104 filesystem bytes remain
identical across virtual motor detection, then saves/reopens a real note.

Thread/timing evidence: `8002CCDC` supplies entry `func_8002C500` to the
thread creation at `8002CCEC`. That input worker calls `func_8004D854` and
`func_8004D450` at `8002C9CC`/`8002C9D4`. Both contain reachable motor-init
calls for primary token `800AAC98`, channel 0:

| Live caller / init call | Gate and argument construction |
| --- | --- |
| `func_8004D854` / `8004D904` | On an accessory-present transition, enabled effects (`80092DE4`) permit detection. `8004D900` adds player*104 to primary base `800AAC98`; the delay slot passes player as channel. |
| `func_8004D450` / `8004D4DC` | The reset flag at `80092DE0` initializes the two primary tokens, beginning with player/channel 0. |
| `func_8004D450` / `8004D5C8` | With latch `80092DEC` clear and `8007D858` zero, initializes the same primary tokens outside active gameplay: `func_80001D4C(8000C560)` returns zero, or `8007D794`/`8007DF44` is nonzero. It sets the latch; active gameplay clears it at `8004D608`. |

The worker snapshots status via `func_8004D968` at `8002C99C`, before
`osContStartQuery` at `8002C9A4`, `osRecvMesg` at `8002C9B4`,
`osContGetQuery` at `8002C9C0`, the detector at `8002C9CC`, and the timer at
`8002C9D4`. The snapshot consists of two N64-endian 32-bit words at
`800DF480 + 4*player`, each assigned `(status[4*player+2] & 1)` by
`8004D97C` through `8004D990`; status starts at `800AAAD0`.

Both arrays initially lie in BSS, cleared over `[800A2BE0, 800E3850)` by
the entry loop at `80000400` through `80000420`. The first effect initializer
`func_8004D79C` also clears both snapshot words at `8004D808`, before the
worker starts (`8002CCB8`). However, a zero snapshot at entry does **not**
imply a first-query transition: startup `func_80001A9C` calls `osContInit`
at `80001C58` with status pointer `800AAAD0` constructed at `80001C4C/50`.
Main startup calls that setup at `80002788`, before installing callback
`8004E8BC` at `800027A0/A4/A8`; that callback calls `func_8004DE04` at
`8004E8E8`, which constructs the worker through `8002CB0C` at `8004E128`.
Thus the first worker snapshot already captures port 0 as present. An
unchanged first query produces no transition and the detector skips Init.

The fixture compiles the pinned runtime's actual `ultramodern/src/input.cpp`
and configures its device callback to `wdc::pak::device_info`. This is the
production runtime implementation, not a translated ROM query body:
generated callers use `osContInit_recomp`, `osContStartQuery_recomp` and
`osContGetQuery_recomp`; `librecomp/src/cont.cpp` forwards those to the same
`ultramodern/src/input.cpp` functions compiled into the fixture. `src/main.cpp`
assigns the production device callback; `recomp::start` forwards it through
`ultramodern::set_callbacks` to `ultramodern::input::set_callbacks`.
Likewise, production `osContGetReadData_recomp` forwards pad reads to that
runtime implementation, whose `get_input` callback is `wdc::input::read` in
the frontend-enabled player. Real
`osContInit`, `osContStartQuery` and `osContGetQuery` produce the status:
controller type `0005` (guest bytes `00 05`), accessory-present byte 1, error
byte 0; other ports report error 8. Only SI completion delivery is replaced
with a synchronous counter. No status bytes are manufactured. After startup,
snapshot and query, the fixture asserts that the detector did **not** initialize.
A timer tick with startup callback `8004E8BC` then reaches the separate
`8004D5C8` Init gate. The real `func_80001D4C` compares the current callback
at `8007D848` to its argument; it returns false because startup is not gameplay
callback `8000C560`. The fixture retains that translated predicate and installs
callbacks through the real setter `func_80001D70`. Installing the gameplay
callback and requesting an effect
produces the first successful Start before any direct native Init call or
standalone helper. The test pins the boot and worker calls, BSS clear, both
live init calls, and queue construction: `lui s7,800B` at `8004D884`,
`addiu s7,s7,AAB0` at `8004D8A0`, then `move a0,s7` at `8004D8E0`.
This is ROM-backed reachability evidence with observed boot-time gate words.
Callback selection and the later pause coverage are controlled, as inventoried
below. It is not a full boot/race playthrough. The SDK initialization gate is retained.

The fixture's game-mode/effect inputs are inventoried below. Every address
uses N64 byte order. Before the first Init/Start, the four gate words are
observed without fixture writes:
the test checks their initialized-image values immediately after loading the
ROM and again after its controller setup, callback installation, effect
initializer, snapshot, query and detector, before the first timer tick.
After the first Start, pause coverage deliberately writes 1 and then 0 to
`8007D858`, asserting that it is still 1 before restoring 0.
These are selected startup operations; the fixture does not execute all of
`func_80001A9C`/`func_8004DE04` or schedule guest threads. The evidence is the
ROM's initial image plus preservation through those executed operations,
not a claim that every later game mode leaves the gates open.

| Input (width) | Initial-image evidence / startup writer | Fixture control and observed use |
| --- | --- | --- |
| `8007D858` (u32) | Zero at ROM `07E458`. The fixture's startup operations preserve it. | No write before first Start. Timer loads it at `8004D5FC/60C` and skips effects when nonzero (`8004D610`). Later pause coverage deliberately writes 1, then restores the observed zero; the ROM's counter writers increment at `80001CB4` and decrement at `80001CF4`. |
| `8007D794` (u32) | Zero at ROM `07E394`, preserved through fixture startup. | Never overwritten; mode checks read it at `8004D52C` and `8004D628`. |
| `8007DF44` (u32) | Zero at ROM `07EB44`, preserved through fixture startup. | Never overwritten; mode checks read it at `8004D53C` and `8004D638`. |
| `800923E8` (u32) | Zero at ROM `092FE8`, preserved through fixture startup. | Never overwritten. The request API `func_8004D9B0` loads it at `8004D9E4`; `8004D9F0` rejects the request when nonzero. The test pins the load's base, load and branch. Its debug use elsewhere does not remove this request gate. |
| `8007D848` (u32 callback) | Zero at ROM `07E448`; setter `func_80001D70` writes it at `80001D74`. Startup installs `8004E8BC` via `800027A4`. | Calls the real setter with startup callback, then deliberately selects gameplay callback `8000C560` for pulse coverage. `func_80001D4C` compares this word at `80001D50`. |
| `80092DE0` (u8 reset) | Zero byte at ROM `0939E0`; asserted before first timer tick. | No direct fixture write; translated code owns reset state. |
| `80092DE4` (u8 enabled) | Zero byte at ROM `0939E4`; first `func_8004D79C` sets it to 1 at `8004D83C`. | Real initializer called before query; no direct fixture write. |
| `80092DEC` (u32 mode latch) | Zero at ROM `0939EC`; asserted before first tick. | Timer sets it to 1 at `8004D604` after Init, then clears it during gameplay at `8004D608`; no direct fixture write. |
| `80092DF0` (u32 initialized) | Zero at ROM `0939F0`; first initializer writes 1 at `8004D848`. | Real initializer controls first-time enable/snapshot reset; no direct fixture write. |

Besides the later pause-gate writes to `8007D858` inventoried above, explicit
guest writes after loading the ROM image are ABI/test storage:
the stack at `807FF000`, note name/extension/number at `80400000/+20/+40`,
and note payload at `80401000`. Request arguments select player 0, delay,
initial motor state, on/off ticks and total ticks through `func_8004D9B0`.
The translated engine writes its own presence/snapshot/timing fields; the
fixture never synthesizes them. The differential physical-probe stubs supply
`FE` transport responses only; the real filesystem initializer restores the
primary object before virtual-motor coverage.

The primary array is also passed to
menu file operations, so the port does not assume global thread exclusivity.
The standalone `func_8004DCA0` helper has no identified direct JAL caller or
raw pointer reference in the reviewed USA image; it is not used as fixture
setup or claimed as the live enable path. The native adapters
are safe on either guest thread because they do not touch the object. They
perform no serial-bus operation, so an SDK SI lock is unnecessary. No guest
pointer is retained. Filesystem reinitialization cannot reset host motor state.
Successful native motor initialization does clear the current host lease;
detection during an active pulse ends that pulse until a later Start command.
The host test covers this reset behavior explicitly.

### Primary-object status and bank reader audit

The audit follows the menu/worker Pak entry points `80071E10`, `800720B0`,
`800723A0`, `80072560`, `800726B0`, `80072800`, `800730AC`, `800734D0` and
`800742D0` through their 28 translated bodies, including ID/checker/inode
helpers. `tests/extract_pfs_test.py` includes these roots and rejects missing
bodies or indirect calls. Primary-address uses in game/menu code pass the
token into this closure or copy ID bytes at `+0C`; none directly reads its
status/bank as a Rumble Pak indicator. Other zero-offset loads in the closure
belong to inode/name/count/stack buffers, not `OSPfs.status`.

These are all primary status-word reads in that closure:

| Reader / load PC | Use of status; effect of bit 8 |
| --- | --- |
| `wdc_pfs_init` / `80072084` | ORs in initialized mask `0x01` after initialization; entry previously clears status. No `0x08` test. |
| `func_800720B0` / `800720F0` | Requires `status & 1`; bit 8 is ignored. |
| `wdc_pfs_free_blocks` / `8007257C` | Requires `status & 1`; bit 8 is ignored. |
| `wdc_pfs_num_files` / `800726CC` | Requires `status & 1`; bit 8 is ignored. |
| `wdc_pfs_allocate_file` / `80072864` | Requires `status & 1`; bit 8 is ignored. |
| `wdc_pfs_read_write_file` / `80073130` | Requires `status & 1`; bit 8 is ignored. |
| `func_800742D0` (menu delete) / `8007430C` | Requires `status & 1`; bit 8 is ignored. |
| `func_80078CA0` (checker) / `8007932C`, `80079344` | Sets or clears corruption mask `0x02`. It preserves other bits without testing motor mask `0x08`. |

Bank-zero guards read `+65`, skip bank selection when zero, and otherwise
set the requested bank to zero and call `func_800788CC`. All such reads are:

| Reader | Load PC(s) |
| --- | --- |
| `func_800720B0` | `80072128` |
| `wdc_pfs_num_files` | `80072704` |
| `wdc_pfs_read_write_file` | `80073168` |
| `func_800734D0` | `800734E0` |
| `func_800742D0` | `80074344` |
| `func_80077CA4` | `80077CC4` |
| `func_800780BC` | `800780D4` |
| `func_80078254` | `80078264` |
| `func_800784B0` | `800784C0` |
| `func_800785AC` | `800785C8` |
| `func_80078CA0` | `80078EDC`, `80078F88` |

The two remaining bank reads have different roles: `wdc_pfs_read_write_file`
at `80073360` compares the selected bank with the note's target bank and
selects it when different; `func_800788CC` at `800788E8` copies the requested
bank into the 32-byte bank-select command. Bank 0 means memory-Pak bank 0,
not accessory presence. The port's one-bank image uses bank 0 for both
metadata and note data. None of these reads interprets bank or status bit 8
as Rumble Pak presence. The reviewed WDC SDK's original motor Start/Stop
instead check their per-channel initialization table at `80099E30`, not an
OSPfs bit-8 guard from a different SDK version; both bodies are replaced.
Game motor presence is held separately at `800DF460 + player` and follows
the detector's return code. The ROM fixture also deletes/counts a note via
the menu's translated path while bit 8 remains clear and bank remains zero.

### Effect and device output

`func_8004D9B0` still creates the original effects; `func_8004D450` retains
their pulse timers, enable checks and game-mode gates. The hook before
`8004D760` renews a 150 ms host lease only for player 1. On the input worker,
it reads the game's u16 effect-pending and motor-on fields at `800DF464` and
`800DF468` with big-endian, word-swapped helpers; both are fixed within 8 MiB
RDRAM. Finished/reset effects cancel the lease. No guest pointer escapes the
call. Replace these addresses and the object identity with named effect and
accessory interfaces when decompiled.

The SDL main thread samples the atomic lease under the existing input mutex,
reads General's saved strength, and borrows the frontend's open handles. It
uses the high-frequency motor, as the shared frontend does, with 150 ms SDL
commands. It does not open additional handles. Per-device output state sends
zero only on the transition to off and refreshes nonzero commands while the
lease holds, even if their strength is unchanged. A failed stop is not latched;
the next effect can run. A failed nonzero command disables further active
writes until that device reconnects; one stop is still attempted. Settings capture, release gating, zero strength,
focus loss, hotplug and normal exit clear feedback; a stalled guest or host
cannot leave an indefinite motor command. Headless/keyboard-only builds have
no physical sink. No dependency patch or change to save-device reporting is
needed.

WDC deliberately owns this sender instead of calling the pinned frontend's
`recompinput::set_rumble()` / `update_rumble()`. That path smooths the rising
and falling edges and uses a 1,000,000 ms dummy duration. WDC retains the ROM's
own pulse timing and bounds every SDL command to 150 ms. General's rumble
option supplies amplitude, persistence and UI; it does not schedule the frontend sender.
`update_rumble()` must not run alongside WDC's sender. The runtime motor callback
is left unset because the three native motor adapters own the guest route.

`wdc_rumble` covers motor identity, no guest-memory access, filesystem preservation,
absent ports, lease expiry, cancellation, output transitions and failed stops.
Its output callback observes calls before SDL/driver duplicate filtering.
`wdc_rumble_rom` runs the real detection, request and effect engine with a
controlled gameplay predicate. It compares the original ROM initializer's
104-byte result, restores the real filesystem, reaches virtual detection through
the live worker's status-transition caller, proves it leaves
it unchanged, allocates/writes a note during an effect, and reinitializes/reopens/reads
that note. `wdc_input` checks actual SDL
virtual-gamepad output, strength, zero mute, settings/focus gating, shutdown and
detach. `wdc_frontend_settings` checks strength persistence and default reset.
These checks do not establish physical gamepad compatibility or racing feel.
Physical acceptance should include PS4/PS5 pads over USB and Bluetooth: the
player enables their direct-device HIDAPI rumble hints before opening them.
Check initial connection, strength, settings/focus muting, reconnect and
save/load alongside feedback, including how those hints affect device opening.
