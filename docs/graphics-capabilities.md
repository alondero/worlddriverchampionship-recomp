# Graphics capability review

Reviewed the local AeroGauge (`e89573f77c60`) and Automobili Lamborghini
(`3254bb4f0db6`) sources: `src/rt64_renderer.cpp`, their configuration modules,
settings/menu code, and player documentation. This was a source comparison,
not sibling gameplay validation.

[Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp) provides a broader
reference: arbitrary rendering rates, widescreen/ultrawide, and texture packs.
Its [release notes](https://github.com/Zelda64Recomp/Zelda64Recomp/releases)
also describe frame-pacing and high-precision framebuffer improvements.

| Capability | Sibling pattern | WDC implementation / boundary |
| --- | --- | --- |
| Higher resolution | Auto window scale, original/2x, downsampling | Auto or explicit native 1-8x scaling |
| Anti-aliasing | MSAA off/2x/4x/8x | Same requested sample counts, device fallback |
| Widescreen / ultrawide | RT64 aspect plus game projection, sky and HUD handling | Camera and horizontal culling follow the window; HUD rectangles remain proportional |
| High rendering rates | Original/display/manual targets and RT64 interpolation | Experimental targets only; no new interpolated WDC motion |
| Fullscreen | SDL window mode, F11 and Alt+Enter | Desktop fullscreen, hidden Windows menu, saved preference |
| Color precision | Auto/standard/high internal framebuffer | Saved startup option; requires restart |
| Filtering | Renderer/presentation options | Nearest/linear/pixel-scaled output |
| Settings | Persistent host configuration and frontend | Tabbed overlay, Apply/Discard, defaults, JSON, Windows menu and shortcuts |
| Texture replacement | Paths and RT64 cache registration | Not added; needs WDC-specific asset validation |
| Draw distance, fog, camera | Game-specific hooks | Horizontal camera aspect only; draw distance and fog unchanged |

## Why camera and motion enhancements are separate

The sibling renderers interpret high-level game display lists. WDC executes
translated BOSS microcode in `src/boss_rsp.cpp`, then calls RT64
`processDisplayLists(..., false)` with low-level RDP batches. Vertices have
already been projected. RT64's `Projection::Type::Triangle` path draws raw
triangles, unlike the perspective/orthographic path with transform metadata.
Changing an RT64 setting alone cannot recover missing world geometry,
unclipped vertices, or stable object identities.

Race captures at 1280x720 confirmed that Expand/16:9 widened the stationary car
by one third without changing its height, while the HUD and scissor retained
their original proportions. The current port instead widens the ROM camera
before projection and culling, then applies the corresponding horizontal
framebuffer scale. It accounts for both 320x240 racing and 320x480 menu buffers
representing the same TV picture.

The camera/viewport hooks and raw-triangle scissor correction are documented
in the [renderer reference](reference/renderer.md). Fixed 2D artwork and
cinematic layouts remain centered. Smooth high-rate motion requires a BOSS-aware
geometry/transform bridge or a validated simulation/render split. Raising VI
frequency would alter timing and is not an acceptable substitute.

The native resolution/4:3/original-rate combination remains available for
comparison. The default increases resolution automatically and matches the
window aspect, while retaining the original presentation rate. Register hooks
run in the guest camera and viewport builders; SDL publishes the output aspect.

## Verification boundary

Host tests cover persistence across restart, replacement, malformed data,
bounds, failed saves and concurrent snapshots. A build or startup diagnostic
does not prove widescreen correctness, motion interpolation, or visual fidelity.
Acceptance of new camera/motion work must include racing, menu transitions,
HUD edges, culling, and split-screen evidence.

Implementation verification (Windows x64, Direct3D 12, RTX 3080): the full
player build passed; all four CTest entries, the Python test suite, documentation
check and whitespace check passed. A 15-second ROM-backed diagnostic initialized
RT64, booted nine guest threads and continued servicing VI events while native
menu commands changed resolution, MSAA, filtering, aspect, presentation target,
and fullscreen. The resulting JSON matched the requested settings. Its exit
code 2 is the expected diagnostic timeout, not a normal player exit. This was
an interaction/startup smoke test, not a racing visual comparison. Linux and
Android builds were not run.
