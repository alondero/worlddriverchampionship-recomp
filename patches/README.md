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

The build scripts apply a series with `scripts/patch_series.py`. The tool owns
the paths a series writes: it restores them to the submodule's pinned index
state, removes the files the series creates, and applies the patches in order.
Local edits to paths a series does not touch are preserved, and a patch that no
longer fits the pinned tree fails the build. Git's reverse-applicability probe
cannot be used on its own here, because patch 0016 rewrites the clipping call
that patch 0014 added; the reverse check for 0014 fails even when its changes
are present.

Each submodule receives exactly one series, so a second run for the same
submodule can never restore away the changes of the first run. `build.ps1` and
`build.sh` declare the same series in the same order, with the Windows-only
patches inside the series of their own submodule, and
`tests/test_build_scripts.py` fails when the two scripts diverge.

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
| `0008-runtime-controller-pak-kind.patch` | N64ModernRuntime | All | Expose the ControllerPak enum for status reporting; WDC owns block storage and retains its ROM filesystem. |
| `0009-runtime-host-config-storage.patch` | N64ModernRuntime | Desktop | Let the overlay use configuration schemas without overwriting host-owned graphics.json. |
| `0010-frontend-enum-wrap.patch` | RecompFrontend | Desktop | Wrap resolution/other choices to fit the overlay's option column. |
| `0011-frontend-runtime-compat.patch` | RecompFrontend | Desktop | Use the shipped event header, match the pinned runtime renderer callback, and link the FreeType DLL with MinGW. |
| `0012-frontend-host-services.patch` | RecompFrontend | Desktop | Allow main-thread SDL services and an explicit executable-relative asset directory. |
| `0013-runtime-rsp-unsigned-packed-loads.patch` | N64ModernRuntime | All | Zero-extend packed vector load bytes before shifting; fixes BOSS shade coefficients for values above 127. |
| `0014-rt64-rdp-screen-clipping.patch` | RT64 | All | Clip reconstructed off-screen vertices in RDP screen space before GPU submission. |
| `0015-rt64-raw-triangle-scissor.patch` | RT64 | All | Scale raw-triangle scissors in the same framebuffer space as their vertices. |
| `0016-rt64-rdp-edge-trapezoids.patch` | RT64 | All | Rebuild raw RDP triangles from their H/M/L edges instead of three extrapolated corners. |

## Application matrix

| Build path | Applied patches |
| --- | --- |
| Linux | 0006, 0007, 0008, 0009, 0010, 0011, 0012, 0013, 0014, 0015, 0016 (not runtime-tested) |
| Windows | 0005, 0004, 0006, 0007, 0008, 0009, 0010, 0011, 0012, 0013, 0014, 0015, 0016 |
| Android | none yet |

The BOSS options default off; existing runtime renderers retain their original
interrupt behavior. Patches are checked against the pinned dependency commits.

Patch 0013 has a synthetic DMEM reproducer in
`tests/test_rsp_packed_loads.cpp`. The pinned runtime and upstream
[N64ModernRuntime helper](https://github.com/N64Recomp/N64ModernRuntime/blob/main/librecomp/include/librecomp/rsp_vu_impl.hpp)
both use signed `RSP_MEM_B` for packed loads. For example, LUV reads `0x80`
as `-128` and shifts it to `0xC000`; the correct result is `0x4000`.
The original [ares implementation](https://github.com/ares-emulator/ares/blob/master/ares/n64/rsp/interpreter-vpu.cpp)
reads unsigned DMEM bytes. LFV and LHV share the faulty seven-bit conversion;
LPV's eight-bit conversion keeps the same final bits while using an unsigned
operand. The patch changes these five reads to `RSP_MEM_BU`, preserving the
existing addressing, element selection, and DMEM wrapping.

Patch 0014 has a ROM-free perspective/coverage reproducer in
`tests/test_rdp_triangle_clip.cpp`. A triangle with a negative W vertex beyond
the scissor loses its visible trapezoid when the GPU clips it in homogeneous
space. The pinned and current upstream
[raw RDP decoder](https://github.com/rt64/rt64/blob/main/src/gbi/rt64_gbi_rdp.cpp)
submit reconstructed vertices directly; the
[vertex shader](https://github.com/rt64/rt64/blob/main/src/shaders/RasterVS.hlsl)
multiplies their screen coordinates by W. The helper clips off-screen triangles
near the RDP scissor first, interpolating reciprocal W and UV/color divided
by W. It uses bounded stack arrays, preserves fully visible positive-W triangles exactly,
and retains existing behavior for a perspective singularity inside visible
coverage. A local probe restored Hawaii's textured sunset; limiting clipping
to negative-W vertices did not restore the complete scene. The regression also
checks positive-W off-screen coverage and guards against numeric overflow.
Conservative rounded bounds include a one-native-pixel margin for RasterVS's
half-pixel offset and raster-scissor rounding at the port's supported
1x-or-higher scales, including widened horizontal output. The GPU scissor
retains final sample clipping; a standard
four-sample MSAA regression verifies edge coverage at 1x, 2x and 3x.
Final player
acceptance is recorded in [first playable](../docs/first-playable.md).

Patch 0015 corrects the pinned RT64 raw-triangle scissor transform. These
vertices already occupy the full scaled framebuffer; applying the projected
camera's inverse aspect scale to their scissor clips both wide edges. The
[renderer reference](../docs/reference/renderer.md) records the pinned source,
a minimal numeric reproducer, and the live GPU acceptance evidence.

Patch 0016 has a ROM-free coverage reproducer in
`tests/test_rdp_triangle_edges.cpp`. The pinned and current upstream
[raw RDP decoder](https://github.com/rt64/rt64/blob/main/src/gbi/rt64_gbi_rdp.cpp)
builds each triangle from three corners extrapolated along H, ignoring the M
and L slopes, so neighbours rebuild a shared edge as two different lines. The
patch adds `rt64_rdp_triangle_edges.h` to emit the trapezoids the RDP edge
walker fills. Each output triangle is clipped by patch 0014, so 0016 applies
after it. The mechanism, measurements and limits are in the
[renderer reference](../docs/reference/renderer.md#raw-triangle-edge-reconstruction).

The overlay patches are shared host UI behavior, copied from the Lamborghini
peer's storage and enum-layout patches at the same dependency pins. The WDC
frontend integration test verifies that opening, editing and discarding
settings cannot write or replace the existing graphics file. Applying changes
queues them for the SDL host; the existing graphics tests cover its persistence.

The pinned frontend's unused bundled renderer expects a newer PresentationMode
argument than the pinned runtime offers. Patch 0011 restores the legacy
callback signature and default renderer presentation, and replaces the stale
external `patches/ui_funcs.h` include with the frontend's shipped event header.
WDC continues using its own BOSS/RT64 renderer.
The MinGW build uses FreeType's shipped DLL import library; the upstream MSVC
static library requires MSVC runtime symbols and cannot link with MinGW.

Patch 0012 keeps upstream defaults for other hosts. WDC injects an SDL-free
system interface and input handler; window size and modifier state are snapshots,
while cursor, clipboard, mouse capture, background hints and error dialogs are
queued to the main thread. Assets resolve beside the executable even when the
launcher runs from the checkout root. Configuration storage is unchanged.
