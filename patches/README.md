# Dependency patch inventory

This directory is the only place for local diffs against pinned public
submodules. The first two entries are toolchain compatibility patches carried
forward from both sibling ports because they are required to compile the
pinned RT64 tree with MinGW on Windows. All game/runtime patches remain gated
on WDC evidence.

## Rules

1. Start from the exact submodule commit recorded by Git.
2. Reproduce the problem with the smallest host, ROM, or renderer case.
3. Decide whether it is WDC policy, a temporary platform fix, or an upstream
   defect.
4. Add the smallest numbered patch and a focused test where possible.
5. Update this inventory, `BUILDING.md`, `build.ps1`, and `build.sh` together.
6. Verify a clean submodule can apply the full series and that a second run is
   idempotent.

The sibling projects demonstrate useful patch categories—scheduler/VI/audio,
RDRAM, frontend integration, interpolation, and Android compatibility—but
their hunks may contain game-specific assumptions. Do not import them before
the WDC runtime demonstrates the same need.

## Patch inventory

| Patch | Dependency | Host | Purpose |
| --- | --- | --- | --- |
| `0004-plume-d3d12-mingw-com-abi-struct-return.patch` | RT64/Plume | Windows | Adapt D3D12 COM struct-return calls for MinGW's ABI. |
| `0005-rt64-mingw-gcc-compat.patch` | RT64 | Windows | Compile RT64's texture hasher with MinGW GCC. |
| `0006-runtime-lle-interrupt-ownership.patch` | N64ModernRuntime | All | Allow the BOSS renderer to own SP/DP completion and intermediate interrupts. |
| `0007-rsprecomp-boss-support.patch` | N64Recomp | All | COP0 callbacks, CFC2/CTC2, exhaustive indirect dispatch option, fail-loud unsupported instructions. |

## Application matrix

| Build path | Applied patches |
| --- | --- |
| Linux | 0006, 0007 (not runtime-tested) |
| Windows | 0005, 0004, 0006, 0007 |
| Android | none yet |

The BOSS options default off; existing runtime renderers retain their original
interrupt behavior. Patches are checked against the pinned dependency commits.
