# Original button cheats (USA)

This is a static analysis of the verified USA ROM described in the
[ROM reference](../reference/rom.md). It establishes the complete dispatch table,
button order and handler behaviour. It does not claim a console playthrough or
visual validation of the unusual renderer options.

Reproduce the decoded table without generated code or optional packages:

```powershell
python -B tools/rom/button_cheats.py --rom "World Driver Championship (USA).z64"
```

Add `--json` for structured output. The tool rejects a different ROM hash.
For instructions, use `tools/rom/inspect_code.py ADDRESS --size SIZE --rom ROM`
with the optional `rabbitizer` package. Addresses below are guest virtual
addresses; file offset = address - `0x7FFFF400`. Inspect the verified ROM,
not an unverified translation, when checking instruction details.

## Complete table

The dispatch table occupies `0x8007DC48` through `0x8007DCF7`: 22 pairs of
big-endian 32-bit handler and sequence pointers. The following pair is null.
Sequences contain newest-first big-endian 16-bit remapped actions, interspersed
with zero releases and terminated by `0xFFFF`.

All directions below mean the D-Pad. Order is oldest press first. Repeated
buttons require releasing the button between presses.

| Effect | Handler | Sequence address | Button order |
| --- | --- | --- | --- |
| Super blurry mode | `0x80002DB4` | `0x8007DBE0` | B, Left, Up, Right, Right, Right, A, Start, Start, Z, Right, A, Up, C-Down, Start |
| Unlock Falcon Challenger | `0x80002AB8` | `0x8007DA14` | Z, Right, Z, C-Down, A, Left, Left, Right, Up, Z, Right, Right |
| Mirror championship | `0x80002AE0` | `0x8007DA48` | Z, Right, Z, Right, Right, Up, Left, Right, C-Down, Z, Right, Down |
| Unlock GT2 events | `0x800029C8` | `0x8007D8FC` | Z, Right, Z, Z, Z, B, Right, Down, Right, Up, Z, Start |
| Unlock GT1 events | `0x800029A0` | `0x8007D8C8` | Z, Right, Z, Z, Z, A, Right, Down, Right, Up, Z, Start |
| Infinite restarts | `0x80002A18` | `0x8007D960` | Z, Right, Z, Right, Right, Start, Z, A, Right, Z, Start |
| Unlock tracks | `0x800029F0` | `0x8007D930` | Z, Right, Z, A, Left, Left, Z, Right, A, C-Down, Start |
| Vestigial renderer flag; visible effect unproven | `0x80002B30` | `0x8007DAA4` | B, Left, Up, Right, A, Right, Up, Left, Z, Right, Right |
| Championship race select | `0x80002A40` | `0x8007D990` | Right, A, C-Down, Right, Start, Right, Left, Right, C-Down, Z |
| Unlock GT2 cars | `0x80002978` | `0x8007D89C` | Z, Right, Z, Z, Z, B, C-Down, A, Right, Start |
| Unlock GT1 cars | `0x80002950` | `0x8007D870` | Z, Right, Z, Z, Z, A, C-Down, A, Right, Start |
| Small cars | `0x80002D94` | `0x8007DBB4` | Left, Right, Right, Z, Left, Right, C-Down, A, Right, Start |
| Unlock Falcon Interceptor | `0x80002A90` | `0x8007D9E4` | Z, Right, Z, Up, Z, Right, Right, C-Down, Right, Z, Right |
| Invisible cars | `0x80002D44` | `0x8007DB64` | Up, Down, Start, B, Left, Right, C-Down, A, Right, Start |
| Open ending sequence | `0x80002A68` | `0x8007D9BC` | Z, Right, Z, A, Left, Left, Left, Up, Start |
| Disable team specials: diagnostic only, no gameplay change | `0x80002B08` | `0x8007DA7C` | Left, Right, Down, Up, Z, C-Down, A, Right, Right |
| Black cars | `0x80002CC0` | `0x8007DB40` | B, Left, A, C-Down, C-Down, A, Right, Start |
| Red skid marks | `0x80002DD4` | `0x8007DC24` | B, Left, Up, Down, Right, Right, Right, Down |
| Top-down camera | `0x80002B50` | `0x8007DAE0` | Up, Up, Up, A, Left, A, A, A |
| Retro track graphics | `0x80002D64` | `0x8007DB90` | Up, Left, A, Z, C-Down, Left, Right, Start |
| Clear scene before HUD | `0x80002CA0` | `0x8007DB20` | C-Down, Left, Up, Right, A, Right, Z |
| Trackside camera mode | `0x80002C2C` | `0x8007DB04` | Up, Right, Right, Z, Down, Down |

The blurry-mode sequence has **no Z between the final Up and C-Down** in this
ROM. The decoded sequence takes precedence over online transcriptions.

## Input scope

`func_80002E7C` records changes to the low action halfword in a 32-entry ring at
controller object + `0x2C`, then compares every sequence and calls matches.
`func_8002C36C` calls that matcher on four controller objects, separated by
`0x7C` bytes. This path has no requirement that cheats use controller two.
`func_80002F58` maps raw D-Pad bits to Right=1, Down=2, Left=4, Up=8;
A=`0x10`, B=`0x20`, C-Down=`0x400`, Start=`0x4000`, Z=`0x8000`.
Analog input is separate and does not supply these low directional actions.

Matching does not suppress normal menu/game actions. Entering a code containing
A or Start can therefore select menus or pause. Camera-affecting handlers are
also constrained by the state they act on, as described below.

## Save and progression effects

The wrappers at `0x80002950` through `0x80002B2C` pass the career object
`0x80097D00` to the underlying handlers. These original handlers write career
state; they are different from the port's session-only availability overrides.

| Underlying handler | Observed effect |
| --- | --- |
| `0x800692B0` | Enables car models 17..33 with `0x800565BC`; enables eight team objects starting at career + `0x144`. |
| `0x80069344` | Enables models 0..16 and eight team objects starting at career + `0x2C`. |
| `0x800693D8` | Calls the event unlocker `0x80066CF8` for event indices 10..19. |
| `0x80069434` | Calls the same unlocker for event indices 0..9. |
| `0x80069490` | Enables all 58 eight-byte track records starting at career + `0xE08`. |
| `0x800694F0` | Enables model 34, then sets team object at career + `0x20C` to class 3 via `0x80056B9C`. |
| `0x80069540` | Enables model 16 and team object at career + `0x11C` via `0x800569FC`. |
| `0x80069590` | Toggles career byte + `0x10FF`; on enable sets restart word + `0x1060` to 9. Setter `0x80066648` reads the byte at `0x80066654` and forces subsequent values back to 9. |
| `0x800695DC` | Toggles career byte + `0x1100`. `0x8006805C` reads it when assembling championship race parameters and sets race + `0x78` to 1. |
| `0x80069610` | Toggles career byte + `0x10FE`. Accessor `0x8006969C` supplies the branch at `0x800589F4`, gated by menu mode 23. |
| `0x80069644` | Sets career byte + `0x10FD` to 1. Accessor `0x800696A8` supplies the branch at `0x800592D4`, forcing menu progress + `0x64` to 7. |
| `0x80069674` | Only calls diagnostic formatter `0x8006F63C` with `cheat disable team specials`; it contains no state-changing gameplay instruction. |

## Renderer and camera evidence

The globals below are game-owned 32-bit words. Observations concern reads by
the translated game thread; this research tooling does not write guest memory.

| Global / handler | Consumer evidence |
| --- | --- |
| Small cars: `0x8007E8D0`, `0x80002D94` | Toggles 0/1. Reads in `func_800218F0` at `0x80022004` (t5), `0x80022260` (t4), `0x80022430` (t7). It changes car translation and matrix scale, including 0.5 in place of the normal 1.0 scale; this is a rendering effect. |
| Visible cars: `0x8007D808`, `0x80002D44` | Toggles 1/0. Load at `0x80021A5C` (t6) in `func_800218F0`; zero exits before car submission. |
| Black cars: `0x8007DF7C`, `0x80002CC0` | Toggles 0/1 and reruns `func_80031674` for each loaded car. That routine reads at `0x80031760` (t9) and `0x800317BC` (t7), retaining only each palette entry's alpha bit for palette groups 1, 2 and 6. Reversing the effect rebuilds palettes from source. |
| Red skid marks: `0x8007DC20`, `0x80002DD4` | Toggles 0/1. `func_80008ED8` reads at `0x80008FF0` (t6); when set, colour bytes 5 and 6 are zeroed while red at byte 4 remains. On enable the original handler also replaces 16 words for each of eight material-table rows at `0x800821B0 + row*64 + 16` with `0x8000001F | (index << 4)`. That mutation is not undone by disabling the original code. |
| Retro track graphics: `0x8007E930`, `0x80002D64` | Toggles threshold 8/0. `func_800230D4` reads at `0x80023244` (t4): normal depth buckets 0..6 choose material root track + `0xF4`; zero makes them choose track + `0x144`. This changes material selection, not polygon count. |
| Super blurry mode: `0x8007E0AC`, `0x80002DB4` | XORs `0xA0` into a framebuffer-blend alpha. `func_80024944` checks nonzero at `0x80024978`; at `0x80024B2C` it uses the word's low byte as primitive alpha while copying the previous framebuffer in strips. The routine additionally requires the active race state and matching framebuffer descriptors. The same global also participates in fades, so XOR does not necessarily alternate between exactly 0 and 160. |

`func_80002B50` sets camera parameters 13.0 and 11.0 via `0x800122D4` and
`0x80012324`, then vector (0, 0, 4) via `0x80012178`, for each local player
scene's embedded camera at scene + `0x11C`. Scenes start at `0x800ABE40`,
size `0x1418`; this handler advances by two scenes (`0x2830`) per iteration.
It is a one-shot camera change, not a reversible flag.

`func_80002C2C` first requires current state callback `0x8000C560`, the active
race. It sets word + `0x1400` of each local scene to 2. `func_8000E1F4`
branches on that value at `0x8000E5BC`, takes camera metadata from the followed
car's current track segment at `0x8000E608`..`0x8000E630`, and interpolates
camera keyframes at `0x8000F134` onward. This establishes trackside camera
selection, not AI control of the player's car. Exact framing remains untested.

## The unusual renderer switches

`func_80002B30` toggles word `0x8007D7FC`. Its consumer is `func_80024D38`:
the load at `0x80024DEC` (t2) makes `0x80024E0C` store `0x2000` at
`0x8007D77C`, or `0x80024E14` store zero. No read of this destination or
aligned ROM pointer to it was found in the measured CPU code/table scan.
The renderer's low-othermode command builder at `0x8002BA6C..0x8002BA98`
loads the two neighbouring words `0x8007D774` and `0x8007D778`, ORs them,
and emits their result after command word `0xE200001C`. It omits the third
word `0x8007D77C`. Thus the cheat's changed word does not enter this command.

The follow-up scan covered every CPU instruction immediate in
`ROM 0x1000..0x7D2B0` and every aligned word in the 16 MiB ROM. It found no
explicit address construction or load of `0x8007D77C`, nor any pointer into
`0x8007D774..0x8007D77F` in KSEG0, KSEG1 or physical-address form. The fixed
RSP transfers constructed by `0x80024F08` also exclude this global: boot
`0x8007C6B0..0x8007C780`, microcode `0x8007C780..0x8007D6E0`, and task data
`0x800A1CD0..0x800A2BE0`, all end-exclusive. The remaining graphics input is
the dynamic display-list pointer/length passed in a0/a1, rather than a copy
of these neighbouring global words.

This supports a vestigial renderer option whose value is omitted from the
command builder; it does **not** establish anti-aliasing, dithering or another
visible effect. The bounded scan cannot rule out an arbitrary computed
indirect alias to the global. No runtime trace or visual comparison was made.

`func_80002CA0` toggles word `0x8007D824`. Its sole direct consumer is the
load at `0x80024FDC` (t6) in `func_80024FCC`. Enabling the word inserts
full-frame scissor setup (`0x800245AC`) and a full-screen fill (`0x800244D0`),
then writes 3 to `0x8007EDBC`, before the framebuffer blend (`0x80024944`)
and HUD (`0x80043274`). Thus it clears the scene before overlays; actual
colour and final appearance have not been measured through BOSS/RT64.

These two switches, the team-specials no-op, and trackside camera code are
described by their instructions rather than inferred from similar cheat lists.
The [TCRF page](https://tcrf.net/World_Driver_Championship) cited by issue #44
could not be retrieved during this investigation, so its four unknown entries
have not been matched to this table by sequence.
