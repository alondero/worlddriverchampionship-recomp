# Renderer reference

RT64 owns general display-list interpretation, texture handling, interpolation,
aspect/presentation semantics, and extended GBI behaviour. The port owns WDC
display-list classification, game-specific projection/HUD policy, and host
configuration defaults.

Before changing RT64, capture the smallest command stream or matrix case that
demonstrates a general semantic mismatch. If the command stream is correct and
the desired result is WDC policy, keep the fix in `src/` or a generated hook.
The headless renderer is a measurement instrument, not the player renderer.

Raw RDP coverage is defined in screen space. A reconstructed triangle can have
a negative perspective W outside the scissor even when all visible samples
have positive reciprocal W. GPU homogeneous clipping then changes coverage.
Patch 0014 clips reconstructed off-screen triangles near the RDP scissor before
GPU submission, preserving the perspective attribute planes. Restricting this
to negative-W vertices did not restore Hawaii's complete sky in live testing.
Fully visible positive-W triangles retain their
original vertices. Conservative rounded bounds include a one-native-pixel
margin for the vertex shader's half-pixel offset and GPU scissor rounding at
the port's 1x-or-higher vertical scales and equal-or-larger horizontal scales. The actual GPU scissor still
clips samples. Singular or negative perspective within the conservative bounds
still uses the existing renderer behavior; this patch does not emulate the
RDP texture divider. See the ROM-free coverage regression and
[patch provenance](../../patches/README.md).

## Window aspect and border-free racing

The USA ROM camera builder `func_80011D54` computes viewport width/height
times the active display descriptor's pixel-aspect correction (`+0x14`).
At `0x80011DC4`, `f0` supplies both camera `+0x7C`'s horizontal visibility
extent and `guPerspectiveF`'s aspect argument. The register-only hook in
`src/game_camera.cpp` multiplies this by output aspect / (4/3), preserving
vertical FOV, depth, viewport partitions and CPU/RSP projection agreement.
The CPU visibility code at `0x80021A18` consumes the same camera extent; the
matrix at `0x800B2A40` also drives point clipping and screen projection.

SDL's main thread publishes the window width/height ratio atomically. The renderer
uses the same factor with RT64 Manual aspect, multiplying its VI-derived
framebuffer aspect rather than treating 320x480 menu pixels as square. RT64's
rectangle path centers and preserves HUD/menu proportions, while raw world
triangles have already been widened by the ROM. Narrow windows retain 4:3.
Non-positive output dimensions reset the published scale to the original TV
aspect, including when the previous output was wide.

The scene-layout builder `func_8000FC18` assigns 8- or 16-pixel TV overscan
insets to `s4/s5`. The hook before `0x8000FEF0` clears those registers before
the ROM builds scene rectangles through `func_8000E148`, so projection,
viewport and scissor expand together. Internal split-screen gaps and cinematic
layouts remain. Neither hook reads guest pointers or changes allocation sizes,
guest VI state or timing. Both run on the game thread, before display-list
submission; generator input in `rom-symbols.toml` owns their exact locations.
Remove them when a guest camera/layout implementation replaces these ROM paths.

Patch 0015 fixes a general raw-RDP mismatch in the pinned RT64 renderer:
`Projection::Type::Triangle` scales screen-space vertices by the framebuffer
scale but was dividing its scissor width by the aspect factor. Minimal case:
320x240 framebuffer, full-width scissor, horizontal scale 4/3 and vertical
scale 1. A raw vertex at x=0 maps to x=0, yet its scissor starts at x=53.33
and ends at x=373.33 inside the 426.67-wide target. Using the same scale for
both restores [0,426.67]. Rectangle compensation remains separate.
This comparison is against RT64 pin `f0728a2520d5aa735886240de3fee75cc805f6d6`,
`src/render/rt64_framebuffer_renderer.cpp`; no current-upstream claim is made.

`wdc_camera_rom` executes the translated camera/perspective/matrix closure with
ROM constants, checking horizontal visibility, preserved vertical/depth terms,
16:9 and ultrawide, non-square-pixel modes, narrow-window fallback and invalid
output resetting a previously wide projection/cull extent. Only the
display-list allocator is replaced in the projection closure. A layout fixture
also runs the real scene-rectangle arithmetic with unrelated mode/orientation
effects stubbed, checking border removal and both split directions. CTest
limits the translated camera/layout execution to 30 seconds.
Live captures verify GPU edge coverage;
the scissor correction has no standalone GPU regression seam yet.

## Gauge triangle alignment

The two gauge layouts in `func_8002E13C` call `func_8002D99C` to build
the tachometer needle as a BOSS screen-space triangle. The dial artwork uses
RDP rectangles. RT64 compensates rectangles for horizontal expansion, but
raw triangles receive the full expansion. Without compensation the needle
stretches and its pivot moves away from the dial in wide windows.

The hook before `0x8002DAA4` corrects only the completed triangle's three
signed 16-bit x coordinates at packet offsets `0x10`, `0x20`, and `0x30`.
They are quarter-framebuffer-pixel coordinates. It maps each x to
`center + (x - center) / width_scale`, where center is twice the active
descriptor's framebuffer width (`+8`). This matches the rectangle path's
centred presentation, including doubled horizontal-resolution modes and
split-screen coordinates. Y, texture coordinates and shared source vertices
are unchanged. The guest-memory contract and bounds are in `game_camera.cpp`.

The ROM-backed camera test executes the actual needle builder with a captured
Hawaii needle fixture, checks final alignment after renderer expansion at
4:3, 16:9 and ultrawide, and checks both horizontal coordinate shifts. This
tests the generated hook as well as its arithmetic.
