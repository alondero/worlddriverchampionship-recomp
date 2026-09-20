# World Driver Championship: Recompiled

World Driver Championship: Recompiled is a planned native PC/Android port of
the Nintendo 64 game using N64Recomp, N64ModernRuntime, RT64, and RecompFrontend.

This repository is the development scaffold. The ROM identity and translated
game image have not been established yet; the first implementation milestone is
to analyse a legally dumped USA ROM and fill in `rom-analysis.toml`.

## Current status

- Repository structure, build contract, documentation rules, and CI scaffolding
  are in place.
- No ROM, generated C/C++, save data, or release binary is included.
- The executable target is a bootstrap placeholder until the ROM-specific
  entrypoint, symbol map, RSP input, and host/runtime wiring are implemented.

## Start here

1. Read [BUILDING.md](BUILDING.md), [Architecture](docs/architecture.md), and
   [Testing](docs/testing.md).
2. Supply your own `World Driver Championship (USA).z64` at the repository root.
3. Measure the ROM header, code extent, entrypoint, and audio microcode before
   changing the generated inputs; record confirmed facts in
   `docs/reference/rom.md`.
4. Fill `rom-analysis.toml`, then run `python scripts/gen_syms_toml.py`.
5. Build and inspect the first generated translation before adding port hooks.

The complete toolchain and platform prerequisites are documented in
[BUILDING.md](BUILDING.md). The sibling-project conventions that informed this
scaffold are recorded in [Peer projects](docs/peer-projects.md).
