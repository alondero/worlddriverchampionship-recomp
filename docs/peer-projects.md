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
- reverse-engineering facts are stored with method/evidence, not copied from a
  sibling game.

## Deliberately deferred

Game-specific symbol names, function boundaries, RSP audio configuration,
renderer patches, input semantics, Android Java activities, and release signing
must be derived from the WDC ROM and its own runtime behaviour. The sibling
projects provide patterns, not evidence for this game.
