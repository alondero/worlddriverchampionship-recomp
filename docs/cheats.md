# Cheats

Open settings and select **Cheats**. Every option starts off each time the
application launches. A change applies immediately, with no Apply button, and
is forgotten when the application closes. Nothing on this tab is written to a
settings file or to the Controller Pak.

| Toggle | Effect | When turned off |
| --- | --- | --- |
| Unlock all tracks | The next track list treats every track as available. | Saved track locks show again. |
| Unlock all GT1 cars | The next GT1 car and team lists treat those entries as available. | Saved GT1 locks show again. |
| Unlock all GT2 cars | The next GT2 car and team lists treat those entries as available. | Saved GT2 locks show again. |

Additional options unlock the Falcon Challenger, Falcon Interceptor, GT1
events and GT2 events. Ten venue options reproduce the original player-name
passwords, including **Lisbon (JIGGLYPUFF)** and **Sydney (FROZENSKY)**.
Each unlock lasts for the session; switching it off restores the saved locks.
A race or menu that is already open does not rebuild itself; leave
it and open the list again. Results driven while a cheat is on can still be
stored by the game. A car you earn while a cheat is on is still written to
the save, because the cheat does not change that write.

| Original effect | When it applies |
| --- | --- |
| Infinite restart credits | Shows nine credits and preserves the earned balance when restarting. Switching off restores that balance. |
| Mirror Championship tracks | When the next Championship race loads. |
| Championship race select | When the Championship menu reopens. |
| Show Championship ending | A request for the next Championship event menu; resets after being read. Switch off to cancel. |
| Pink cars (IGN64), black cars | When cars next load. Black takes priority if both are active. |
| Small cars, invisible cars | While drawing cars; physics and collisions continue. |
| Red skid marks | While drawing new skid marks. |
| Retro graphics | While selecting track materials. |
| Super blurry mode | While blending race frames; native transition fades keep their own alpha. |
| Clear scene before HUD | A debug effect which clears the scene before drawing overlays. Can obscure the race view. |
| EVILROB McCloud tune | On the next race load with the McCloud F-Type A. Uses the original alternate physics and conditional R-button power boost. |
| Bird's-eye camera | While racing, using the original elevated camera parameters. |
| Trackside cameras | While racing. The player still controls the car. Takes priority over Bird's-eye camera. |

Turning these options off restores native behavior at the same boundary.
If a name or button cheat is already active in the original game, turning off
the overlay option leaves that original effect active. Appearance, BOSS/RT64
rendering, and driving feel have not been validated in a playthrough for these
new effects; the evidence below is static analysis and focused host/translated
function tests.

The original "disable team specials" code only prints a diagnostic. Another
renderer switch has no established visible consumer. Neither has a menu
option. Their exact instructions and all 22 button sequences are recorded in
[original button cheats](research/original-button-cheats.md). See also the
[12 name checks](research/original-name-cheats.md) and
[complete Quick Race reward table](research/original-track-unlocks.md).

The sections below are for people changing the port.

**Restore defaults** on the Cheats tab turns every toggle off. The Controls
tab keeps its own reset and does not change these toggles.

## What the toggles replace

The USA ROM has button-code appliers for the same three groups. The menu does
not enter those codes and does not call the appliers. The appliers write the
save; these toggles only change what a locked read returns.

Measured press order, oldest button first, matches the codes published for
this game:

- Tracks: Z, D-Pad Right, Z, A, D-Pad Left, D-Pad Left, Z, D-Pad Right, A,
  C-Down, Start. The ROM sequence is at `0x8007D930`.
- GT1 cars: Z, D-Pad Right, Z, Z, Z, A, C-Down, A, D-Pad Right, Start.
  Sequence at `0x8007D870`. Applier `0x800692B0` covers car models 17 through
  33 and eight team objects from save+324.
- GT2 cars: Z, D-Pad Right, Z, Z, Z, B, C-Down, A, D-Pad Right, Start.
  Sequence at `0x8007D89C`. Applier `0x80069344` covers car models 0 through
  16 and eight team objects from save+44.

Sequences are stored newest-first, with a zero halfword between presses, and
compared with the low half of the remapped action word from `func_80002F58`.
Team id 7 is in both car groups. Model 16 is the challenger and is included
with GT2. Model 34 is the interceptor (`0x800694F0`) and is not included.

The additional overlay controls reproduce the understood effects of those
original codes through the boundaries below, rather than calling their
save-writing appliers.

## Native boundary

ROM SHA-256: `e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97`.
Hooks are generated from `rom-symbols.toml`. Do not edit generated C. The UI
thread publishes `std::atomic_bool` flags. Hooks run on the game thread that
calls the accessor, after its `lbu` and before `jr ra`. `ra` still names the
caller. Only the menu return addresses listed in `src/game_cheats.cpp` are
changed. The Controller Pak snapshot uses the same accessors and is not in
those lists: track reads at `0x800648D0` and `0x8006491C`, car byte 0 at
`0x80064B44` and `0x80064B88`, and car byte 1 at `0x80064840` and `0x80064888`.
`func_80056B18` writes a team byte when car byte 1 reads as unlocked, so that
caller is excluded too. `0x80056A88` still sees a real lock and can store a
car the player earns. An unknown caller keeps the loaded byte. The hooks do
not retain guest pointers, call SDL, or write guest RAM.

Some allowlisted callers then store that returned byte into a larger runtime
object. The track list stores it at offsets 1416, 1467, and 1468. The team
class check `func_80066558` (return `0x80066584`, which is allowlisted) can
store 7 at offset 8276 when a team reads as available. Those instructions look
like menu state, not the 8-byte track record and not the note packed by
`0x80064800` and `0x800649C4`. A later copy of those objects into the
Controller Pak was not found, and a full trace did not rule it out. This was
read from the instructions around the calls, not from a playthrough.

| Accessor | Hook | Override |
| --- | --- | --- |
| `func_800565E0` | before `0x800565E4` | `lbu v0, 0(a0)` already ran. For a menu caller, if `v0` is zero, read the big-endian model id at record+8. Models 0..16 follow GT2; models 17..33 follow GT1. Models 16 and 34 also have individual Challenger/Interceptor controls. |
| `func_800565EC` | before `0x800565F0` | `lbu v0, 1(a0)` already ran. Byte 1 is the companion flag `func_800565BC` stores as 1 together with byte 0. The ROM has no name for it. Same model test and the same toggles, for the menu callers that read this byte. |
| `func_80056974` | before `0x80056978` | `lbu v0, 16(a0)` already ran. For a menu caller, if `v0` is zero, read the big-endian team id at record+0. Ids 0..7 follow GT2; ids 7..14 follow GT1. Challenger also enables team 6; Interceptor enables team 12. |
| `func_80057000` | before `0x80057004` | `lbu v0, 5(a0)` already ran. For a menu caller, if `v0` is zero and all-tracks is on, set `v0` to 1. Otherwise read the u32 layout id at record+0; ids 0..57 follow the venue toggle for id/6. |

A non-zero loaded value is left alone, so an earned unlock stays an earned
unlock. `func_80056B18` stores 1 at a team object's byte 16 when
`func_800565EC` returns non-zero. That return address is not overridden.
`func_80056A14` calls `func_800565E0` at `0x80056A88` and, when `v0` is zero,
calls `func_800565BC` at `0x80056A98` to store the car unlock. That return
address is not overridden, so earning a car still writes the save.

Pinned N64Recomp normally emits direct host calls without the MIPS `jal`
write to `ra`. The reviewed `link_register_targets` in `rom-symbols.toml`
make `scripts/gen_syms_toml.py` publish the real sign-extended call+8 at all
46 direct calls to the ten caller-sensitive accessors/setter. This includes
save and progression calls, so they cannot inherit a previous menu's link.
The generator rejects pointer-table references, tail jumps, hook conflicts or
incomplete call/delay ranges for these targets. `tests/test_cheat_callers.py`
checks that measurement policy; the translated tests also execute complete
menu/progression callers starting with unrelated or stale return addresses.

The usual career object is `0x80097D00`. Car records are 20 bytes at +2892
(35 slots). Team objects are 40 bytes at +44. Track records are 8 bytes at
+3592; the track applier walks 58 of them and stores at byte 5. The hooks
use the record pointer they are given and do not assume that base.

Each word read is one aligned KSEG0 address in the 8 MiB guest RAM, through
`MEM_W`. A null context, null RDRAM, unaligned address, or address outside
`0x80000000..0x807FFFFC` leaves `v0` unchanged. The track hook does not read
the record when all-tracks is enabled; venue overrides read its layout id. Replace
these with named availability checks once the menus are decompiled.

`wdc_cheats` calls the hooks with synthetic RDRAM. `wdc_frontend_settings`
checks that the tab applies immediately, ignores `cheats.json`, and that
Restore defaults on Cheats clears the flags while the Controls tab index does
not.

## Additional game boundaries

All hooks are generated from `rom-symbols.toml`. Every control is an atomic
host boolean; the UI never touches RDRAM. Guest accesses below run on the
consuming game thread in the verified USA image, use word-swapped `MEM_W`
for N64 big-endian u32/IEEE float32 values, retain no guest pointers after the
call, and leave original behavior on null/invalid memory. They can be removed
when these routines have named source interfaces.

- Event accessor `0x80057420`, before `0x80057424`: unlocked byte at +0x4C,
  u32 event id at +0x50 of a 112-byte record. Only menu returns `0x8005B298`
  and `0x80066520` override zero; GT2 ids 0..9, GT1 ids 10..19.
- Team class `0x80056B90`, before `0x80056B94`: u32 class at +20, u32 id at
  +0 of a 40-byte team. Interceptor forces class 3 for id 12 only at menu
  returns `0x80058464`, `0x8005AE80`, `0x8005E788`; the progression reader
  `0x800681FC` stays native.
- Restart count `0x8006663C`, before `0x80066640`: return 9 only to
  `0x80045AD4`, `0x80045AE8`, `0x80046788`. Save return `0x80064B24` sees the
  real balance. Before setter `0x80066648`, only return `0x80045AFC` reads
  the real aligned u32 at career+0x1060 back into a1, preserving that balance
  instead of saving the synthetic 9-1. Bounds cover base through the word.
- Ending accessor `0x800696A8`, before `0x800696AC`: only return
  `0x800592D4` consumes the atomic request and returns 1. It never changes
  the original ending byte at career+0x10FD.

Other hooks replace a register after the original read, with no new guest
memory access. Off preserves the exact original register value.

| Effect | Before instruction(s) | Register(s) and enabled value |
| --- | --- | --- |
| Pink | `0x80031758` | v1 = 1, after native flag/name decision |
| Black | `0x80031764`, `0x800317C0` | t9, t7 = 1 |
| Small | `0x80022008`, `0x80022264`, `0x80022434` | t5, t4, t7 = 1 |
| Invisible | `0x80021A60` | t6 = 0 |
| Red skid marks | `0x80008FF4` | t6 = 1 |
| Retro materials | `0x80023248` | t4 = 0 |
| Blur | `0x8002497C`, `0x80024B30` | t6, t7 = 160 only if original alpha is zero |
| Clear before HUD | `0x80024FE0` | t6 = 1 |
| Mirror Championship | `0x80068060` | t5 = 1 |
| Race select | `0x800696A0` | v0 = 1 |
| EVILROB | `0x80036518` | v0 = 0 (strcmp matched), after primary-car/model-27 gates |

The overlay's red-skid option changes only the color producer. It deliberately
omits the original handler's permanent material-table mutation. Blur overrides
only the two framebuffer consumers, leaving the shared fade timers unchanged.
Camera hook ownership, bounded temporary fields, and restore behavior are
documented next to `src/game_cheat_camera.cpp`.

`wdc_cheats_rom` executes the translated accessor/setter closure and requires
the generated hooks to exist. It checks original loads, caller isolation,
on/off behavior, one-shot consumption and preservation of the complete career
in each permutation. A representative accessor scenario additionally checks
all synthetic RDRAM once. `wdc_cheat_camera` checks both camera records,
bounds, restoration, and recovery from missing or invalid exits without heap
allocation. The nonrecursive camera routine uses one fixed snapshot; disabled
entry leaves it untouched, and every exit consumes any active snapshot.
