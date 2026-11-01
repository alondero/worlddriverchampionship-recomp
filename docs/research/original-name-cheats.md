# Original player-name cheats

This records static analysis of the USA ROM identified in the
[ROM reference](../reference/rom.md), for [issue #44](https://github.com/alondero/worlddriverchampionship-recomp/issues/44).
It does not establish PAL behavior or claim a console/emulator playthrough.

Recheck names, comparison sites, unlock loops, venue names and the non-track
consumer instructions with:

```text
python -B tools/rom/name_cheats.py --rom "World Driver Championship (USA).z64"
```

The tool validates the reviewed SHA-256 before interpreting the ROM. All
addresses below are N64 virtual addresses; ROM offset is address minus
`0x7FFFF400`. Words and floating-point parameters are big-endian. This is
read-only offline evidence, with no guest pointers retained by the tool.

## Search boundary

There are **12 direct calls** to the string comparator `0x80019F34` in the
measured CPU code (`ROM 0x1000..0x7D2B0`, end exclusive). Every call is preceded
within 20 instructions by `0x80056D24`, the accessor that returns the player
name. Ten are together in `0x80065928`; two are in car initialization.
This enumerates direct calls to that comparator, rather than proving there
could be no other comparison implementation or dynamically generated code.

The ROM stores the following comparison strings in lowercase; spelling below
is literal, including `frozensky`, rather than the sometimes reported
`frezensky`. The comparator is byte-exact and the name copier at `0x80056CBC`
copies at most 12 bytes followed by a terminator. No code for a
`RACES THE BEST` player-name comparison was found by this search.

## Track-family passwords

Starting a Championship game runs `0x80065928` after its career initialization.
Each matching name loops through a range of track records, calls
`0x800673FC(career, index)` to obtain the record, then calls
`0x8005700C(record, 1)` to set its availability byte at record+5. These are
original game writes to career state; the overlay's read overrides have a
different persistence contract.

| Name | Venue | Career track indices, inclusive | Comparison | String |
| --- | --- | --- | --- | --- |
| `fritz` | Les Gets | 6..11 | `0x80065A5C` | `0x800A0EEC` |
| `webstein` | Hawaii | 0..5 | `0x80065AA4` | `0x800A0EF4` |
| `vortex` | New Zealand | 18..23 | `0x80065AEC` | `0x800A0F00` |
| `uncanny` | Las Vegas | 12..17 | `0x80065B34` | `0x800A0F08` |
| `stuffy` | Kyoto | 48..53 | `0x80065B7C` | `0x800A0F10` |
| `jacqueline` | Zurich | 42..47 | `0x80065BC4` | `0x800A0F18` |
| `skateordie` | Rome | 30..35 | `0x80065C0C` | `0x800A0F24` |
| `jigglypuff` | Lisbon | 24..29 | `0x80065C54` | `0x800A0F30` |
| `jackedup` | Black Forest | 54..57 | `0x80065C9C` | `0x800A0F3C` |
| `frozensky` | Sydney | 36..41 | `0x80065CE4` | `0x800A0F48` |

Venue identity comes from the first word of each 80-byte track descriptor at
`0x80096534 + 80 * index`, followed by the name pointer at
`0x80090950 + 68 * venue_id`. `0x80056F88` and `0x80056F4C` perform those same
lookups. Thus `jigglypuff` unlocks Lisbon's track variants; it has no separate
car-scale or other graphical effect in this handler.

## Pink cars: `ign64`

The literal is at `0x8009B214`, compared at `0x8003174C` inside the car palette
builder `0x80031674`. The preceding native flag read is
`lw v1, 0x8007DF80` at `0x80031720`; a nonzero flag bypasses the name comparison.
At `0x80031758`, `v1` is the combined native flag/name decision. The following
palette loop processes banks 1, 2 and 6 using the original luminance-derived
pink conversion. The separate black-car flag `0x8007DF7C` takes priority.

An overlay hook can force `v1=1` before `0x80031758`, leaving the native flag
and career name intact. The appearance changes when the game next rebuilds
the palette; disabling a read override does not reconstruct an existing one.

## McCloud tuning: `evilrob`

The literal is at `0x8009C6C0`, compared at `0x80036510` inside the physics
initializer `0x800364A0`. It only applies when both conditions already hold:

- The containing car is the primary car, `0x800BA7F0`.
- The selected model is 27, whose name pointer at
  `0x80081030 + 128 * 27` identifies **'99 McCloud F-Type A**.

A match stores 1 in the runtime word `0x80092350` and changes the physics row
index (`s3`) to 35 at `0x8003652C`. The normal path clears that runtime word
for the primary car. The initializer reads 160-byte physics rows at
`0x80090CD0 + 160 * index`. Row 35 is an alternate McCloud tune, with changed
parameters and gearing; it has no ordinary selectable model/name record.
For example, row offsets `0x28` and `0x38` change from 930 to 630 and from
447000 to 596000 respectively. Those are observed table values; a resulting
speed or lap-time improvement was not measured here.

`0x80016DF8` reads `0x80092350` at `0x80016EC0`. For the primary car, when
physics byte `+0xCF8` bit 0 is clear and R is held (remapped action bit 20), it
multiplies the output of `0x80017530` by 1.5 at `0x80016F14`. This is an
additional conditional effect of the original tune, not an unrestricted
speed multiplier for every car. The controller remapper `0x80002F58` maps the
raw R mask `0x0010` to `0x2000` at `0x80003034..0x80003044`, then creates the
upper alias `0x00100000` at `0x800030E8..0x800030FC`.

To reproduce the name behavior from the overlay, force comparison result
`v0=0` before `0x80036518`, after the original primary-car/model checks.
That uses the original runtime flag and initialization writes and preserves
native name activation when the overlay option is off. The change applies on
the next car initialization. No career name or saved car model needs changing.
