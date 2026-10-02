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
the port's supported 4:3, 1x-or-higher scales. The actual GPU scissor still
clips samples. Singular or negative perspective within the conservative bounds
still uses the existing renderer behavior; this patch does not emulate the
RDP texture divider. See the ROM-free coverage regression and
[patch provenance](../../patches/README.md).
