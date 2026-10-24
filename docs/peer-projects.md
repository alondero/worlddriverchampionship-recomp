# Peer-project conventions

This project is the next member of the same N64Recomp port family. The
AeroGauge and Automobili Lamborghini repositories were compared before this
scaffold was created.

## Retained conventions

- recursive submodules pin N64ModernRuntime, RT64, and RecompFrontend;
- `build.ps1` and `build.sh` are the single desktop build contract;
- the build configures twice because generated sources do not exist on the
  first pass;
- ROMs, saves, captures, build trees, and generated output are ignored;
- generated files are protected by an agent hook and documented ownership;
- patch application is explicit, ordered, idempotent, and checked against the
  pinned submodule;
- docs checks, host tests, and `git diff --check` run before hand-off;
- CI fetches ROM data only from a private asset source and removes it before
  packaging;
- menus and settings use the shared RecompFrontend overlay, which both
  siblings ship as their in-built RT64 menu, instead of a native window menu
  that would only work on Windows;
- reverse-engineering facts are stored with method/evidence, not copied from a
  sibling game.

## Deliberately deferred

Game-specific symbol names, function boundaries, RSP audio configuration,
renderer patches, input semantics, Android Java activities, and release signing
must be derived from the WDC ROM and its own runtime behaviour. The sibling
projects provide patterns, not evidence for this game.

## Controller Pak implementation

[AeroGauge's block device](https://github.com/alondero/aerogauge-recomp/blob/1ace3fdd6c2ad8af5aef0cf34602dad05c549663/src/aero_pak.cpp)
retains the ROM's SDK filesystem and replaces status and 32-byte read/write
calls, with synchronous image replacement. This is the WDC seam: addresses
were matched independently against WDC ROM bytes, then inspected at call sites.

[Lamborghini's bridge](https://github.com/alondero/automobililamborghini-recomp/blob/f1542f04e21894b1f1c315f7a255c1f4a0840eaf/src/libultra_stubs.c)
instead parses Joybus command frames because that game uses a custom SI path.
WDC does not need to reproduce Joybus framing or CRC transport at the chosen
SDK block boundary. Both siblings retain the game's note-management code.

[Mario Kart 64's game registration](https://github.com/sonicdcer/MarioKart64Recomp/blob/3f72d2385cc995fea79e8fca81d886cb2bdce1de/src/main/main.cpp)
uses cartridge EEPROM (`Eep4k`); setting a cartridge save type cannot implement
WDC's Controller Pak. The pinned WDC runtime returns error 1 from its PFS
replacements. WDC therefore gives the filesystem entries non-SDK symbol names
to keep their real translated bodies, exposing only the ControllerPak enum
through a dependency patch. See [save configuration](configuration.md).

## Rumble implementation

[Lamborghini's motor bridge](https://github.com/alondero/automobililamborghini-recomp/blob/e398f066e50c4d61b5ab7db95cac861d2fdc66fd/src/libultra_stubs.c)
keeps its Controller Pak visible and adapts the game's motor wrappers and PWM
engine. Forcing a Rumble Pak flag would break its save flow. WDC likewise
keeps save detection truthful. Its worker shares the primary Pak object with
file operations, so the adapter retains filesystem flags/metadata while
initializing motor fields and leaves the original effect engine translated.

[AeroGauge's haptics](https://github.com/alondero/aerogauge-recomp/blob/023b13ef0d77adc1753b3e92d9da5d621f075405/src/aero_haptics.cpp)
observe damage and turbo timers and publish bounded feedback to a main-thread
SDL sink. WDC already has native effect requests, so it needs no synthetic
collision/turbo policy. It adopts short device leases to contain pause/stall
behavior.

[Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/b65c482ed672258eb684ab94a4e8c8aa662615ee/src/game/input.cpp)
and [Starfox64Recomp](https://github.com/sonicdcer/Starfox64Recomp/blob/f2996555d73ca480f3dc3639f375c50818847bfc/src/game/input.cpp)
separate motor commands from SDL output, offer saved strength, and drive the
high-frequency motor. Their virtual accessory reports Rumble Pak; WDC retains
Controller Pak for its own filesystem. WDC uses its existing frontend device
ownership and saved General setting, with a bounded duration instead of their
long dummy motor duration. See [the WDC boundary](reference/input.md#rumble-alongside-saves).
