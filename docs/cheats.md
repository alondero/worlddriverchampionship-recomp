# Cheats

Open settings and select **Cheats**. The three toggles start off each time the
application launches. A change applies immediately, with no Apply button, and
is forgotten when the application closes. Nothing on this tab is written to a
settings file or to the Controller Pak.

| Toggle | Effect | When turned off |
| --- | --- | --- |
| Unlock all tracks | The next track list treats every track as available. | Saved track locks show again. |
| Unlock all GT1 cars | The next GT1 car and team lists treat those entries as available. | Saved GT1 locks show again. |
| Unlock all GT2 cars | The next GT2 car and team lists treat those entries as available. | Saved GT2 locks show again. |

Championship event locks stay as saved. The Falcon Interceptor stays on its
own unlock. A race or menu that is already open does not rebuild itself; leave
it and open the list again. Results driven while a cheat is on can still be
stored by the game. A car you earn while a cheat is on is still written to
the save, because the cheat does not change that write.

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

Separate ROM cheats for events, infinite restarts, mirror championship, race
select, the win sequence, and team specials are not toggles.

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
| `func_800565E0` | before `0x800565E4` | `lbu v0, 0(a0)` already ran. For a menu caller, if `v0` is zero, read the big-endian model id at record+8. Models 0..16 follow GT2; models 17..33 follow GT1. Set `v0` to 1 only when that toggle is on. |
| `func_800565EC` | before `0x800565F0` | `lbu v0, 1(a0)` already ran. Byte 1 is the companion flag `func_800565BC` stores as 1 together with byte 0. The ROM has no name for it. Same model test and the same toggles, for the menu callers that read this byte. |
| `func_80056974` | before `0x80056978` | `lbu v0, 16(a0)` already ran. For a menu caller, if `v0` is zero, read the big-endian team id at record+0. Ids 0..7 follow GT2; ids 7..14 follow GT1. Set `v0` to 1 only when a matching toggle is on. |
| `func_80057000` | before `0x80057004` | `lbu v0, 5(a0)` already ran. For a menu caller, if `v0` is zero and the track toggle is on, set `v0` to 1. One toggle covers every track record. |

A non-zero loaded value is left alone, so an earned unlock stays an earned
unlock. `func_80056B18` stores 1 at a team object's byte 16 when
`func_800565EC` returns non-zero. That return address is not overridden.
`func_80056A14` calls `func_800565E0` at `0x80056A88` and, when `v0` is zero,
calls `func_800565BC` at `0x80056A98` to store the car unlock. That return
address is not overridden, so earning a car still writes the save.

The usual career object is `0x80097D00`. Car records are 20 bytes at +2892
(35 slots). Team objects are 40 bytes at +44. Track records are 8 bytes at
+3592; the track applier walks 58 of them and stores at byte 5. The hooks
use the record pointer they are given and do not assume that base.

Each word read is one aligned KSEG0 address in the 8 MiB guest RAM, through
`MEM_W`. A null context, null RDRAM, unaligned address, or address outside
`0x80000000..0x807FFFFC` leaves `v0` unchanged. The track hook does not read
the record; a null context or null RDRAM still leaves `v0` unchanged. Replace
these with named availability checks once the menus are decompiled.

`wdc_cheats` calls the hooks with synthetic RDRAM. `wdc_frontend_settings`
checks that the tab applies immediately, ignores `cheats.json`, and that
Restore defaults on Cheats clears the flags while the Controls tab index does
not.
