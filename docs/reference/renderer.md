# Renderer reference

RT64 owns general display-list interpretation, texture handling, interpolation,
aspect/presentation semantics, and extended GBI behaviour. The port owns WDC
display-list classification, game-specific projection/HUD policy, and host
configuration defaults.

Before changing RT64, capture the smallest command stream or matrix case that
demonstrates a general semantic mismatch. If the command stream is correct and
the desired result is WDC policy, keep the fix in `src/` or a generated hook.
The headless renderer is a measurement instrument, not the player renderer.
