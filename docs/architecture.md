# Architecture

The intended port has four modules with explicit seams:

```text
ROM -> N64Recomp/RSPRecomp -> generated game image
                                  |
                                  v
guest threads/RDRAM <-> N64ModernRuntime <-> RT64 presenter
                                  ^
                                  |
                        hand-written WDC port
                                  ^
                                  |
                         SDL/RecompFrontend host
```

The generated image is an implementation detail. Its stable interface is the
entrypoint, translated function symbols, RSP task functions, and the narrow
hooks described by the generated configuration. The port should keep the
interface small and put policy behind deep modules: input translation, file or
Controller Pak access, presentation, audio routing, and evidence probes.

## Ownership

- N64Recomp and RSPRecomp own translation output.
- N64ModernRuntime owns guest scheduling, OS replacements, RDRAM, VI timing,
  and runtime events.
- RT64 owns display-list interpretation, texture handling, interpolation, and
  presentation semantics that are general across games.
- RecompFrontend owns settings UI, assets, fonts, and controller configuration.
- This repository owns WDC ROM policy, game-specific hooks, host wiring, tests,
  and small compatibility patches.

## Thread boundaries

SDL event polling and window operations stay on the SDL main thread. Guest
memory reads and writes occur on the documented translated-game boundary.
Graphics work flows through RT64's graphics path. Any cross-thread request uses
an explicit synchronized hand-off; a global guest-memory write is not a
synchronization primitive.

## Transitional bridges

Direct RDRAM writes, synthetic display lists, and address-based hooks may be
needed during early reverse engineering. Treat each as a temporary adapter,
with a contract and a removal path toward named source/decompilation, stable
symbols, explicit source patches, or a versioned mod/asset interface.
