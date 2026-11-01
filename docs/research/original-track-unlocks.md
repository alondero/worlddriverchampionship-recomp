# Original Quick Race rewards

This answers the track-unlock part of [issue #44](https://github.com/alondero/worlddriverchampionship-recomp/issues/44)
for the verified USA ROM identified in the [ROM reference](../reference/rom.md).
The findings come from static ROM data and the instructions in its race setup
and result handlers. They have not been confirmed by completing these races
in the game or a reference emulator.

## What the game checks

The five published car/course combinations do unlock their reported tracks,
but the reward handler checks **the selected car and the number of laps**,
not the course. Those courses happen to supply the required lap counts.
The complete table has 50 track-award conditions covering 48 distinct tracks,
plus 26 car-award conditions. There are 64 combinations with no award.

| Published car/course combination | Default laps | Reward |
| --- | --- | --- |
| Stallion C, Hawaii A | 2 | Las Vegas CR |
| Reeds R12 Manta C, Las Vegas A | 3 | Sydney CR |
| Rage 512 Evo A, Hawaii C | 2 | Kyoto AR |
| Rage 512 Evo C, Lisbon C | 3 | Hawaii C |
| Elan Swift TT C, Lisbon CR | 4 | Kyoto B |

The ROM's car label for the Stallion is `Stallion SR`; the championship team
may describe it differently. `R` in track labels below means the reverse
course, independently of the mirror setting.

A qualifying result has:

- Race mode `0x800923E4 == 3`, **Quick Race > Arcade Race**.
- Player finishing position `0x8007E130 == 0` (first place).
- A lap count of 2, 3, 4, or 5 at `0x800923E0`.

The reward handler does not read the course, reverse flag, mirror flag,
difficulty, elapsed time, or current availability of the reward. In particular,
there is no requirement to repeat one specific course to earn each reward.
This is a statement about the inspected handler and its setup/caller, not a
claim that every menu exposes every possible lap count on every course.
The separate Championship event rewards follow their own progression rules.

## Tracks left after Championship

The 20 Championship event definitions at `0x80095C90` cover 50 distinct
tracks. The six initially available tracks are ids 0, 1, 12, 26, 29 and 54;
all six also occur in Championship. Eight tracks are absent from those events:

| Track | Quick Race route |
| --- | --- |
| Les Gets AR | Stallion SR B, 3 laps |
| Les Gets BR | Elan Swift TT B, 4 laps |
| Las Vegas CR | Stallion SR C, 2 laps |
| Sydney AR | Reeds R12 Manta B, 3 laps |
| Sydney CR | Reeds R12 Manta C, 3 laps |
| Zurich AR | Rage 512 Evo B, 4 laps |
| Zurich BR | No ordinary reward in the inspected tables |
| Kyoto AR | Rage 512 Evo A, 2 laps |

Thus the tables explain additional post-Championship track routes beyond the
five published examples. Zurich BR (id 46) is the sole course absent from both
ordinary reward systems and the initially available set. The original game's
Zurich name password and all-tracks button cheat can make it available; see
the [name-cheat findings](original-name-cheats.md) and
[button-cheat findings](original-button-cheats.md).

## Track awards

Each row requires a first-place finish with the named car and lap count.
Use a course with that lap count; the last column is the signed big-endian
halfword holding the reward track id.

| Car | Laps | Track unlocked | Reward halfword |
| --- | --- | --- | --- |
| '96 Rage 512 Evo C | 3 | Hawaii C | `0x800962C4` |
| '97 Rage 512 Evo B | 4 | Zurich AR | `0x800962DA` |
| '98 Rage 512 Evo A | 2 | Kyoto AR | `0x800962E4` |
| '97 Reeds R12 Manta C | 3 | Sydney CR | `0x800962FA` |
| '98 Reeds R12 Manta B | 3 | Sydney AR | `0x8009630C` |
| '99 Reeds R12 Manta A | 2 | Lisbon BR | `0x8009631A` |
| '99 Reeds R12 Manta A | 3 | Rome A | `0x8009631E` |
| '97 Stallion SR C | 2 | Las Vegas CR | `0x8009632C` |
| '98 Stallion SR B | 3 | Les Gets AR | `0x80096342` |
| '98 Stallion SR B | 4 | Hawaii CR | `0x80096346` |
| '99 Stallion SR A | 4 | Kyoto A | `0x80096358` |
| '96 Elan Swift TT C | 4 | Kyoto B | `0x8009636A` |
| '97 Elan Swift TT B | 3 | Sydney A | `0x80096378` |
| '97 Elan Swift TT B | 4 | Les Gets BR | `0x8009637C` |
| '98 Elan Swift TT A | 5 | Zurich A | `0x80096392` |
| '98 Furio LS B | 2 | New Zealand BR | `0x80096398` |
| '98 Furio LS A | 2 | Les Gets A | `0x800963AA` |
| '98 Furio LS A | 3 | Hawaii BR | `0x800963AE` |
| '98 Ram Venom GTR B | 4 | Rome C | `0x800963C4` |
| '99 Ram Venom GTR A | 2 | Black Forest B | `0x800963CE` |
| '99 EXR Challenger A | 2 | Lisbon BR | `0x800963E0` |
| '99 EXR Challenger A | 4 | Sydney B | `0x800963E8` |
| '97 Hayai N70 C | 3 | Kyoto C | `0x800963F6` |
| '97 Hayai N70 B | 4 | Lisbon A | `0x8009640C` |
| '97 Hayai N70 B | 5 | Black Forest BR | `0x80096410` |
| '97 Hayai N70 A | 2 | Kyoto CR | `0x80096416` |
| '97 Hayai N70 A | 3 | New Zealand B | `0x8009641A` |
| '98 Lance Scorcher GT B | 2 | Las Vegas B | `0x8009643A` |
| '98 Lance Scorcher GT B | 4 | Zurich CR | `0x80096442` |
| '98 Lance Scorcher GT A | 4 | Les Gets CR | `0x80096454` |
| '97 Elan Scorpion C | 2 | Rome CR | `0x8009645E` |
| '97 Elan Scorpion C | 4 | Sydney BR | `0x80096466` |
| '97 Elan Scorpion B | 3 | Sydney C | `0x80096474` |
| '97 Elan Scorpion A | 3 | Rome BR | `0x80096486` |
| '97 Elan Scorpion A | 5 | Zurich B | `0x8009648E` |
| '98 McCloud F-Type B | 2 | Kyoto BR | `0x80096494` |
| '98 McCloud F-Type B | 4 | Lisbon AR | `0x8009649C` |
| '99 McCloud F-Type A | 2 | Rome AR | `0x800964A6` |
| '99 McCloud F-Type A | 3 | Black Forest AR | `0x800964AA` |
| '98 Panzer Batraye GT B | 2 | New Zealand AR | `0x800964B8` |
| '98 Panzer Batraye GT B | 4 | Zurich C | `0x800964C0` |
| '99 Panzer Batraye GT A | 3 | New Zealand C | `0x800964CE` |
| '99 Panzer Batraye GT A | 5 | Les Gets CR | `0x800964D6` |
| '96 Rage 996 GTR B | 2 | Les Gets B | `0x800964DC` |
| '96 Rage 996 GTR B | 4 | Hawaii AR | `0x800964E4` |
| '98 Rage 996 GTR A | 2 | Las Vegas AR | `0x800964EE` |
| '98 Rage 996 GTR A | 3 | Les Gets C | `0x800964F2` |
| '99 M-K Phantom R A | 2 | Lisbon B | `0x80096500` |
| '99 M-K Phantom R A | 4 | New Zealand CR | `0x80096508` |
| '99 EXR Mystic A | 2 | Rome B | `0x80096512` |

## Car awards

The same mechanism also unlocks these cars. The table stores a negative
model id for each car award. These awards set car byte 1, the companion flag
used by Quick Race and team availability reads, then refresh team availability.
They do not directly set car byte 0, which the Championship car accessor reads.

| Car used | Laps | Car unlocked | Reward halfword |
| --- | --- | --- | --- |
| '97 Rage 512 Evo B | 2 | '98 Stallion SR B | `0x800962D2` |
| '97 Reeds R12 Manta C | 4 | '97 Rage 512 Evo B | `0x800962FE` |
| '98 Reeds R12 Manta B | 5 | '99 Stallion SR A | `0x80096314` |
| '99 Reeds R12 Manta A | 5 | '98 Furio LS B | `0x80096326` |
| '98 Stallion SR B | 2 | '98 Reeds R12 Manta B | `0x8009633E` |
| '99 Stallion SR A | 5 | '99 Reeds R12 Manta A | `0x8009635C` |
| '97 Elan Swift TT B | 5 | '98 Elan Swift TT A | `0x80096380` |
| '98 Elan Swift TT A | 4 | '99 Ram Venom GTR A | `0x8009638E` |
| '98 Furio LS B | 5 | '97 Elan Swift TT B | `0x800963A4` |
| '98 Furio LS A | 4 | '99 EXR Challenger A | `0x800963B2` |
| '99 Ram Venom GTR A | 3 | '98 Furio LS A | `0x800963D2` |
| '99 EXR Challenger A | 3 | '97 Hayai N70 B | `0x800963E4` |
| '97 Hayai N70 B | 3 | '98 Lance Scorcher GT B | `0x80096408` |
| '97 Hayai N70 A | 4 | '98 Lance Scorcher GT A | `0x8009641E` |
| '98 Lance Scorcher GT B | 5 | '97 Elan Scorpion C | `0x80096446` |
| '98 Lance Scorcher GT A | 2 | '97 Elan Scorpion B | `0x8009644C` |
| '97 Elan Scorpion C | 3 | '97 Hayai N70 A | `0x80096462` |
| '97 Elan Scorpion B | 4 | '98 McCloud F-Type B | `0x80096478` |
| '97 Elan Scorpion A | 4 | '99 Panzer Batraye GT A | `0x8009648A` |
| '98 McCloud F-Type B | 3 | '98 Panzer Batraye GT B | `0x80096498` |
| '99 McCloud F-Type A | 5 | '97 Elan Scorpion A | `0x800964B2` |
| '98 Panzer Batraye GT B | 3 | '96 Rage 996 GTR B | `0x800964BC` |
| '99 Panzer Batraye GT A | 2 | '98 Rage 996 GTR A | `0x800964CA` |
| '96 Rage 996 GTR B | 3 | '99 McCloud F-Type A | `0x800964E0` |
| '98 Rage 996 GTR A | 4 | '99 M-K Phantom R A | `0x800964F6` |
| '99 M-K Phantom R A | 3 | '99 EXR Mystic A | `0x80096504` |

## Evidence and reproduction

Run the decoder against a locally supplied big-endian USA dump:

```text
python -B tools/rom/quick_race_rewards.py --rom "World Driver Championship (USA).z64"
python -B tools/rom/quick_race_rewards.py --rom "World Driver Championship (USA).z64" --json
```

The tool rejects a size or SHA-256 mismatch against `rom-analysis.toml` before
parsing. It reads the tables directly, requires no generated recompiler output
and writes no save or guest memory. Its addresses use the CPU startup mapping
`ROM offset = VRAM - 0x7FFFF400`.

The instruction evidence can be inspected with the optional `rabbitizer`
dependency and the existing ROM disassembler:

```text
python -B tools/rom/inspect_code.py 0x800662EC --size 0x154 --rom "World Driver Championship (USA).z64"
python -B tools/rom/inspect_code.py 0x80068D90 --size 0x110 --rom "World Driver Championship (USA).z64"
```

| Location | Evidence |
| --- | --- |
| `0x80099024..0x80099047`, `0x80059EB8..0x80059F14` | Quick Race menu rows 1/2/3 point to the strings `practice laps`, `time attack`, `arcade race`; the selection handler maps them to race modes 8/5/3 respectively. |
| `0x8004C890..0x8004C978` | Result dispatch selects `func_800662EC` for mode 3. |
| `0x8000CD74..0x8000CDA4` | Race ranking writes the zero-based sorted position to `0x8007E0B0`+racer-index*`0x8C`+`0x80`. Racer 0's slot is `0x8007E130`, the word read by the award handler. The result display uses the same word to highlight the player's row at `0x8004ACC4..0x8004ACF4`. |
| `0x800662EC..0x8006643C` | Reward handler rejects a nonzero player position, selects one of four columns by lap count, and applies the signed reward. It contains no course comparison. |
| `0x80066368..0x800663E0` | Model id from config+8 (`0x80092368`) indexes 18-byte records; lap counts 2/3/4/5 select reward halfwords at `0x800962C0`, `0x800962C4`, `0x800962C8`, `0x800962CC`, plus model*18. |
| `0x800663EC..0x80066428` | A negative reward calls `func_800673D0` with its negation, sets car byte 1 through `func_800565F8`, and calls `func_80066B70`; a positive reward calls `func_800673FC` and sets track byte 5 through `func_8005700C`. Zero does neither. |
| `0x800673FC..0x8006740C` | Track record is career+`0xE08`+track-id*8. |
| `0x80068B70..0x80068B88` | Quick Race setup calls `func_80057024` for the selected course and places its default lap count in config+`0x80`. |
| `0x80057024..0x80057044` | Course default lap count is the word at `0x8009653C`+track-id*80. |
| `0x80068DD8..0x80068E8C` | The preceding halfwords (`0x800962BE`, `0x800962C2`, `0x800962C6`, `0x800962CA`, plus model*18) populate config+`0x94`/`0x98` with an event/profile selector split by quotient and remainder 6. These are not a condition comparing the selected race course. |
| `0x8000506C..0x800050C8` | Config+`0x94`/`0x98` selects a 0x120-byte race/AI setup record in ROM; it does not replace the selected course in config+`0x70`/`0x74`. |
| `0x800962BC..0x80096531` | 35 reward records, each an AI rank and four (profile, signed-reward) pairs of halfwords. |
| `0x80057294..0x80057360`, `0x80095C90..0x80095F0F` | Championship constructor reads 20 event records with 32-byte stride; the word at +4 is the course count and up to six following words are course ids. Their union contains 50 distinct ids. The result path `func_800660CC` makes each completed event course available through `func_8005700C`. |
| `0x80056F20..0x80056F48` | Initial track availability byte comes from `0x80096538`+track-id*80. Only six of the 58 defaults are nonzero. |
| `0x800563BC..0x800563D4` | Car names are pointers at `0x80081030`+model*128. |
| `0x80056F4C..0x80056FFC` | Track metadata at `0x80096534`+track-id*80 supplies city id, reverse byte (+12), and layout id (+16); city names are pointers at `0x80090950`+city-id*68. |

All addresses above describe the original game data and code. No new runtime
bridge is introduced by this investigation. The writes described are game-owned,
run on the game thread during result handling, and change the active career.
A subsequent game save can therefore preserve the original-game awards.
