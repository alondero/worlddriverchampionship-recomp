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

## Raw triangle edge reconstruction

An RDP triangle command has no vertices. The edge walker fills spans between
the major edge H (YH..YL) and the minor edges M (YH..YM) and L (YM..YL); XH
and XM are given at scanline floor(YH), XL at YM. RT64's raw decoder
(`decodeTriangles`, pin `f0728a2520d5aa735886240de3fee75cc805f6d6`) builds
three corners: H at YH, H at YL, and (XL, YM), ignoring the M and L slopes.
BOSS encodes an edge shared by two triangles with the same slope in both,
so their RDP edges coincide. Its H slopes, however, can disagree with the
same triangle's minor edges by about 0.3%, so H extrapolated to YL misses
L's end. Each neighbour then rebuilds the shared edge differently. The RDP's
eight coverage samples per pixel hide such gaps; GPU sample centres expose
them as dotted seams that worsen with resolution.

Patch 0016 submits the walked area instead: H against M from YH to YM and H
against L from YM to YL, as two trapezoids. Rows where the minor edge has
crossed H are dropped, following the edge walker's empty spans. Shade,
texture, W and depth come from the unchanged coefficient planes, offset from
floor(YH) and from H on the vertex's row. Each triangle still passes through
patch 0014's screen clip.

`WDC_RDP_CAPTURE` and `tools/rdp/analyze_seams.py` measure complete BOSS
tasks, pairing neighbours under both geometries. On captured Hawaii frames
at a 4.8x scale, the three-point shapes left on the order of a thousand
uncovered sample centres along tens of shared edges; the edge areas left
tens, where BOSS's own neighbouring edges differ by up to 0.25 pixels. The
ROM-free regression `tests/test_rdp_triangle_edges.cpp` includes a pair
reduced from such a capture.

Known limits: the split at YM adds a vertex part-way along H, a T-junction
that GPU vertex snapping can still crack at sub-pixel scale. Gaps present in
BOSS's own edges remain. The inverted-span cut follows edge-walker semantics
but is not checked against a reference. Live checks covered Hawaii practice
at automatic scale, native 1x with 4x MSAA, and 6x; no reference-console or
emulator comparison was performed.

## Window aspect and border-free racing

The USA ROM camera builder `func_80011D54` computes viewport width/height
times the active display descriptor's pixel-aspect correction (`+0x14`).
At `0x80011DC4`, `f0` supplies both camera `+0x7C`'s horizontal visibility
extent and `guPerspectiveF`'s aspect argument. The hook in `src/game_camera.cpp`
maps the viewport's fraction of the full logical picture to the output aspect,
preserving vertical FOV, depth, viewport partitions and CPU/RSP projection
agreement. It computes `viewport_aspect / logical_picture_aspect * output_aspect`.
The ROM's pixel correction alone is insufficient: cinematic modes assume a
letterboxed VI, while the pinned RT64 presenter fills the output with the sampled
picture. Keeping that assumption squeezed cars to half their intended width,
including at Original 4:3.

At this hook, `f8` holds viewport width/height and `t6` already holds the active
descriptor loaded from `0x8007D75C`. The hook reads its signed 32-bit logical
width/height at `+0/+4`, in pixels, through word-swapped RDRAM helpers. These are
ROM-owned values initialized by `func_800006E0`, which populates the entire
nine-descriptor table (modes 0..8) before any scene layout. For example,
instructions `0x80000748` and `0x80000768` store 320 and 240 in the boot-default
mode 0 at `0x800AAE38`.
The layout builder `func_8000FC18` selects a descriptor and reads its sizes at
`0x8000FE48`/`0x8000FE54`; it does not initialize them. Both frame paths,
`func_8000C560` and `func_8004E950`, call layout, publish the selected descriptor
through `func_800010A0`, then build the scene camera. Display initialization must
precede those frame paths. The ROM regression checks all nine descriptors'
logical and buffer dimensions immediately after initialization, before layout,
and executes the boot-default camera successfully. A 96-case sweep of layouts
0..7, variants 0..3 and player values 0..2 selects only modes 1, 3, 6 and 8.
Descriptors 2, 4, 5 and 7 are initialized but not selected by that builder in this
domain; this is not a global unreachability claim about other entrypoints.

The game thread reads eight aligned bytes before task submission, retaining no
pointer. Invalid guest ranges still fail before access. Dimensions outside
1..4096 instead use the ROM-computed aspect **times the output width scale**,
preserving the pre-PR widening behavior. Omitting the scale would leave the ROM's
original horizontal field of view while the renderer expands the output width,
making cars too wide.
Tests cover seven zero, negative, oversized and non-size pairs at four output
widths with pixel corrections 1 and 2 (56 fallback cases). The reference camera
uses correction 1 at the same output size; the expected cull extent is multiplied
by the tested pixel correction and the horizontal matrix term divided by it.
Vertical and depth terms must stay unchanged. This checks the ROM's pixel
correction as well as output scaling and rejects a plain 4:3 fallback.
One warning per process avoids frame-by-frame log flooding; later malformed
states are intentionally silent.

Before this hook, the ROM computes `f0` as viewport width/height times its pixel
correction. The fallback multiplies that aspect by the output width scale.
The HUD packet adapter retains its existing fail-fast policy before guest-memory writes: an
invalid buffer width breaks the packet's coordinate/centre invariant, so it
stops before modifying the packet. Both hooks retain pointer-range validation.
Buffer dimensions at `+8/+12` are deliberately excluded: modes 6 and 8 use
320x240/640x240 logical pictures with 320x480/640x480 buffers. The in-race pause
menu's NORM / H-RES toggle switches the layout request/current words at
`0x8007E57C`/`0x8007E578` from 0 to 1: normal mode 1 becomes mode 6. The
independent descriptor-variant word `0x8007E5B8` remains 0 in that live check.
Mode 3, used by menus, has a 320x480 logical picture and pixel correction 2.
Modes 6 and 8 now use the same physical output proportions as normal racing;
inset cinematic viewports and split-screen partitions retain their relative bounds.
The CPU visibility code at `0x80021A18` consumes the same camera extent; the
matrix at `0x800B2A40` also drives point clipping and screen projection.

SDL's main thread publishes the window width/height ratio atomically. The renderer
uses the same width factor `S` with RT64 Manual aspect, setting its render target
to `source * S`, where `source = decodeVI().fbSize().x / fbSize().y`.
This target describes framebuffer scaling, not the final physical picture.
RT64's workload divides target by source, yielding horizontal/vertical resolution
scales `(M * S, M)` for resolution multiplier `M`. WDC disables
`presentation.removeBlackBorders`, so the pinned `VIRenderer::getViewportAndScissor`
uses a fixed **320x240** TV canvas, not `fbSize()`, and VI view/crop rectangles
are the unit rectangle. The final physical aspect is therefore `(4/3) * S` in
every mode. This is the camera hook's output aspect even when the VI source is
not 4:3. The presenter still samples `fbSize() * resolutionScale` from the texture.

The ROM-backed test compiles the pinned RT64 VI decoder and extracts the real
presenter coordinate transforms, replacing only GPU value/size interfaces.
For field 0, the ROM-initialized registers produce these decoded values (all
have `VI_H_START = 0x006C02EC`; both fields are tested):

| Mode | VI width | VI X/Y scale | VI V start | RT64 `fbSize()` | Source aspect |
| --- | --- | --- | --- | --- | --- |
| 1 | 320 | `0x200` / `0x400` | `0x002501FF` | 320x240 | 1.333333 |
| 3 | 320 | `0x200` / `0x02000800` | `0x002301FD` | 320x476 | 0.672269 |
| 6 | 320 | `0x08000200` / `0x02000800` | `0x009A0188` | 320x240 | 1.333333 |
| 8 | 640 | `0x400` / `0x02000800` | `0x009A0188` | 640x240 | 2.666667 |

At 16:9 the render targets are approximately 1.777778, 0.896358, 1.777778 and
3.555556; the production presenter returns a physical 16:9 viewport for all four.
These are repeatable decoder/presenter measurements from ROM-initialized VI
state, not a new live capture. Mode 3 retains RT64's existing four-row sampling
estimate (476 of 480 logical lines); the test separately checks its 476/480
displayed width/height ratio rather than claiming exact square proportions.
Expected logical and sampled dimensions are independently specified in the test,
so reading buffer dimensions in place of logical dimensions fails it.

RT64's rectangle path preserves normal HUD/menu proportions, while raw world
triangles have already been widened by the ROM. Narrow windows retain 4:3.
Non-positive output dimensions reset the published scale to the original TV
aspect, including when the previous output was wide.

The scene-layout builder `func_8000FC18` assigns 8- or 16-pixel TV overscan
insets to `s4/s5`. The hook before `0x8000FEF0` clears those registers before
the ROM builds scene rectangles through `func_8000E148`, so projection,
viewport and scissor expand together. Internal split-screen gaps and cinematic
layouts remain. The layout hook reads no guest pointers; neither hook changes
allocation sizes, guest VI state or timing. Both run on the game thread, before
display-list submission; generator input in `rom-symbols.toml` owns their exact locations.
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
also runs the real scene-rectangle arithmetic with unrelated heap/orientation
effects stubbed, checking border removal and both split directions. CTest
limits the translated camera/layout execution to 30 seconds. The test also runs
the ROM's real display initializer, descriptor lookup and mode selector, covering
normal/Hi-Rez racing, menus and cinematics at 4:3, 16:9, ultrawide and narrow
output. Projected width/height after VI sampling and the CPU cull extent must
match the actual presenter viewport and scene fraction in each mode, including
both split directions. The two VI fields give 80 presentation cases. The nine
initialized descriptors, selector sweep and 56 malformed-size/output/pixel-correction combinations
check initialization, mode selection and fallback.
Only framebuffer allocation is replaced in display initialization; descriptor
construction and copies run translated code. This is deterministic projection
coverage, not visual acceptance of every scene or display transition.
Windows/D3D12 Hawaii practice was checked live through the pause menu's NORM /
H-RES toggle. Both descriptors now produce the same physical 3D proportions;
the unchanged view direction and narrow H-RES FOV can still change car size.
H-RES sprites and the gauge needle are corrected in the section below. Replays,
other tracks and split-screen have not been visually accepted for this change.
Live captures verify GPU edge coverage;
the scissor correction has no standalone GPU regression seam yet.

## H-RES sprite and needle proportions

Modes 6 and 8 draw a 240-line picture into a buffer twice that tall
(`320x480` and `640x480`). The race scissor stays `0..240` and RT64 samples
those 240 lines. The VI's start lines (`0x9A..0x188`) put that picture in a
half-height band on a console, so each line is half as tall as in normal
racing. The game therefore doubles sprite heights in H-RES. It keeps
positions in the 240-line picture: sprites grow about anchors the game picks,
not about line 0. This port fills the window with the picture, so the doubled
sprites look twice as tall. Mode 3 is a real 480-line menu, where logical
height equals the buffer, and nothing below applies to it.

Live Hawaii practice captures (`WDC_RDP_CAPTURE`, Windows/D3D12, 1296x759
window) show the guest packets before any correction, NORM then H-RES, in
picture lines:

| Element | NORM | H-RES | Anchor |
| --- | --- | --- | --- |
| Speed digits | 189..209 | 180..220 | gauge centre, about 198 |
| Track map | 168..232 | 104..232 | map bottom, 232 |
| Map marker | 199..203 | 166..174 | map bottom, 232 |
| Timing text | 10..21 | 10..21, narrower font | not doubled |
| Pause panel | 90..156 | 60..192 | picture centre, 120 |
| Pause text | 95..105 | 70..90 | picture centre, 120 |

The race HUD builder `func_8002E13C` has a separate H-RES layout, with a
different gauge, and no single anchor maps it back. Three inputs select it:

- The stack flag at `sp+0x1F0` (stored at `0x8002E1A4`: buffer height > 256)
  moves layout edges. At `0x8002EDF4` the same flag, with a logical height
  below 384, sets `0x80080640`, which later picks the narrow timing font.
- The scene's 0x18-byte HUD configuration at `scene+0x2C4`, held in `a1`, sets
  the gauge kind and its offsets. `func_8000FC18` copies it from a ROM table
  chosen by layout `0x8007E578`, variant `0x8007E5B4` and player count
  `0x8007E580`. A normal one-player race copies `0x8007E5BC` (big dial). The
  live one-player H-RES race (mode 6, variant 0) copies `0x8007E61C`: the small
  dial, placed for the doubled picture. Mode 8 (variant 1) copies `0x8007E64C`,
  which already has the big dial and no Y shift.
- The Y shift at `0x8007FE84`, copied from that configuration at `0x8002E52C`,
  makes `func_8002DAD0` and the needle builder double Y. The builder computes
  each position in picture lines and shifts it right by that amount before the
  blitter shifts it back.

`wdc_hud_normal_layout`, before `0x8002E1A8`, clears the stack flag and any Y
shift left by an earlier drawer such as `func_8004D120`. `wdc_hud_normal_shift`,
before `0x8002E530`, clears the copied Y shift. In mode 6 it also points `a1`
at the entry a normal race with the same player count copies (`0x8007E5BC`, or
`0x8007E5D4` for two players). After that point the builder only reads through
`a1` (saved at `sp+0x1C4`), so neither the scene nor the ROM table is written.
Both hooks run only when the active descriptor's buffer is twice its logical
height. A bad descriptor pointer keeps the ROM's layout. Mode 8's wider X
still uses the scene's X shift and the wide flag at `sp+0x1F4`. Mode 8 and
two-player H-RES have not been seen live.

Every other doubled sprite measured grows about the picture's centre line.
The in-race pause drawer reaches `func_8006DA18` (text), `func_8006D22C`
(panel) and `func_8006CA7C` (the flipped panel edges, opcode 37). With the
H-RES flag set and sprite byte `+0x1C` clear, these emit step `0x02000100`.
The pause initializer `func_80050E24` stores that byte as 0, and
`func_8006C910` does not write it. The race-start countdown `func_8004D120`
sets the Y shift itself when the buffer is taller than 256 lines, places each
sprite at `((y0 + h / 2) >> 1) - 9` and lets `func_8002DAD0` double it, so
its digits carry step low half `0xFF00` instead of `0xFE00`.

The graphics worker rewrites a texrect whose step proves that double:
`0x02000100` on opcode 36 or 37, or a digit low half of `0xFF00` on opcode 36.
Both Y edges become `centre + (y - centre) / 2`, with the centre at half the
logical height, and the step returns to its normal value. Every pause
rectangle in the capture then has NORM's Y. The game's own H-RES pause layout
sits 2 pixels further right, and X is not changed. Step `0x02000200` is not
rewritten. It is both the flag-clear rectangle and the packet with sprite
byte `+0x1C` set, which moves the origin without doubling the height.
`func_8006CA7C` doubles both screen Y edges and stores `dtdy` 0x100. RT64
applies `dsdx` along screen Y and `dtdy` along screen X. Halving the Y edges
and restoring `dtdy` to 0x200 matches the NORM packet on both texture axes.
The digit rule stays on opcode 36: on a flip that low half is the X step.
The background divider `func_80024944` emits `0x04000400` and is not an H-RES
double.

The rewrite is a 16-byte stack copy, submitted with `processDisplayLists`. X
and the S/T word are unchanged, and the guest command stream is not written.
Pause text is outside the HUD marker, so the rule applies to every texrect.
Menu edge-fill brackets still see the original art coordinates. A texrect
whose coefficient sits in the next BOSS chunk keeps its first word until that
coefficient arrives. If the task ends without it, the original first word is
handed to RT64's pending-command path.

The picture size comes from the ROM-owned descriptor pointer at `0x8007D75C`
(`+4` logical height, `+12` buffer height, word-swapped signed 32-bit pixels).
The game thread publishes the pointer before task submission. The graphics
worker reads it from RDRAM at the start of the task, not from the BOSS chunk,
which may be DMEM. An unusable pointer skips the correction and does not abort.

`wdc_camera_rom` runs `func_8000FC18` for every layout, variant and player
count. In each mode-6 case, `wdc_hud_normal_shift` must leave `a1` on bytes
equal to what layout 0 copies for the same player count; mode 8 keeps its
own entry. It runs `func_8004D120` in mode 1 and mode 6 at four countdown
times. The correction of each mode-6 packet must equal the mode-1 packet. It
also runs the real needle builder after `wdc_hud_normal_shift`. Mode 6 keeps
the normal Y, mode 3 keeps its doubled Y, and a clear flag is unchanged.
`wdc_hud_triangle` checks both layout hooks in modes 6, 8, 3 and 1, with one
and two players, and with a bad descriptor pointer. `wdc_hud_stream` checks the countdown packets and the
captured pause packets, including the flipped edge's texture spans, and walks
the worker's submission decisions. The shifted `+0x1C` packet, the
flag-clear packet, the divider, a coefficient split across two chunks, a task
that ends on the held word, a menu edge-fill bracket and HUD alignment against
the original X are covered. That walk does not call RT64. The HUD builder
itself is not executed by a test.

Live check (Windows/D3D12, 1296x759 window, Hawaii practice, NORM then
H-RES switched from the pause menu): with both hooks, the H-RES race frame
draws the same 41 HUD rectangles as NORM, at the same screen positions and
with the same steps. Only the digit glyphs (clock and speed values) and the
map marker's position differ. The big dial, needle, map and timing font match
the NORM screenshot, and the H-RES pause menu is centred.

## Race HUD placement

The USA HUD builder `func_8002E13C` draws the gauge sprites and needle,
map and markers, and timing/rank/lap sprites. Hooks at `0x8002E51C` and
`0x8002FA78` bracket its instrument/timing portion. The earlier wrong-way
message and the subsequent pause/menu draws retain centered alignment.
Early returns bypass both hooks; the ending hook is also guarded against an
unpaired call.

`src/game_hud.cpp` reserves a 64-byte raw-list packet from the ROM's descending
frame arena (`0x8007FE74`) and links it through the ascending BOSS cursor
(`0x8007ED84`). The packet carries an `E0574443` marker, viewport horizontal
bounds, and the ROM's `DF` terminator. The full block accommodates BOSS's
64-byte DMA reads. Both cursors are u32 guest pointers; allocation bounds,
alignment and cursor collision are checked before any writes. The game owns
the arena and its lifetime. These game-thread writes use N64Recomp's
word-swapped memory helpers and occur before task submission.

BOSS dispatches raw opcode E0 through `0x17DC` unchanged (the USA DMEM
dispatch entry at ROM `0xA34AE`). The renderer consumes the marker on its
graphics worker before RT64 sees it, without modifying guest commands.
Command framing survives DMA chunk boundaries and treats triangle/texrect
continuations as payload. Each HUD texrect is anchored to its nearest active
viewport edge, rebasing the original coordinate by that edge's pixel position.
This keeps the observed map, gauge and timing groups together. Viewport edges
become proportional RT64 origins, so internal split edges remain internal.
The renderer saves/restores rectangle alignment and scissor state, expands
horizontal clipping while retaining vertical limits, and closes any outstanding
bracket at the task boundary.

Placement uses the peer ports' Original / Clamp16x9 / Full policy, defaulting
to Clamp16x9. RT64's extended aspect target is scaled by the current framebuffer
aspect because WDC also uses non-square pixels. The needle correction below
adds `(anchor - width/2) * (effective_width_scale - 1)` before undoing raw
triangle stretch. Original has zero travel; Clamp16x9 caps the width scale at
4/3; Full follows the camera scale. This extends PR #9's proportional needle
fix rather than replacing it. Remove these bridges when a guest HUD
implementation can emit its own explicit alignment.

Verification includes ROM-backed needle/arena checks for all three settings,
4:3, narrow and wide outputs and doubled horizontal coordinates; a DMA framing
fixture; persistence and staged menu tests. Windows/D3D12 Hawaii practice was
checked live at 4:3, widescreen and a 1680x720 ultrawide window, switching all
three options and accelerating to 47 mph. The map, timing and dial moved
together, the needle retained its hub, and the pause panel stayed centered.
Split-screen and other race variants use viewport-relative origins but have
not been visually verified. Captures remain local ignored evidence.

## Menu frame edge fill

Menu screens frame a widened 3D scene with header and footer bands. The
USA band builder `func_800500E0(object)` blits the band art once at
`0x80050104` through the tiled sprite blitter `func_8006DA18` (x 8, y from
object `+0x58`, ten 152x12 strips at 2x horizontal magnification). It then
draws title overlays. `func_8005063C(object)` blits a separator bar once at
`0x8005065C` through `func_8006CCD4`. Every screen with this frame calls
each builder for its top and bottom pair. The art spans x 8..312 of the
320x480 menu buffer, and RT64 keeps these texrects at 4:3 proportions.

Hooks before and after each of those two calls emit HUD-style marker
packets (payload `0xFFFFFFFE` to begin, `0xFFFFFFFF` to end) through the
same frame-arena adapter. The begin hook reserves room for its end packet
and skips the bracket when the arena is full; the effect is cosmetic.
Inside a bracket, the renderer submits each complete texrect, then draws
fills for edges within the 16-pixel TV overscan margin. Each fill spans
from the art edge to the output edge with `G_EX_ASPECT_STRETCH`, where
framebuffer x 0..width covers the output. It uses the art's tile, t and
dtdy, with s fixed at the outer pixel centre and dsdx 0, so it repeats the
outer texel column. It overlaps the art by one pixel to hide rounding and
rect misalignment, and runs under a full-width copy of the scissor.
`wdc::hud::edge_fills` owns the geometry. RT64 shrinks regular rects about
the frame centre by the camera width scale, so fills vanish at 4:3 and in
narrow windows. Copy and fill cycle modes are skipped.

Buttons, scrolling title text, banners, icons and panels are outside the
brackets and keep their original placement. Scrolling text and banners are
clipped at the art's original edge by the game's scissor; a fill would
streak them as they scroll. A texrect split across BOSS DMA chunks keeps
its first word across `send_rdp` calls, and its fills follow the
continuation word; no fixture covers that split yet.

`wdc_menu_rom` executes the translated band and bar builders with their
generated hooks: it checks that the art blits alone are bracketed, that
title overlays fall outside, and that a full arena skips the bracket. The
host stream test checks fill geometry at 16:9, 2:1, the overscan margin,
4:3, narrow and degenerate rects. Windows/D3D12 was checked live on the
main and Quick Race menus at 16:9 and about 2.3:1, with Original 4:3
unchanged. Hawaii practice HUD and pause panel were also unchanged. The
seam where a fill meets the art showed no gap. Other menu screens built
from the same builders have not been inspected individually.

## Gauge triangle alignment

The two gauge layouts in `func_8002E13C` call `func_8002D99C` to build
the tachometer needle as a BOSS screen-space triangle. The dial artwork uses
RDP rectangles. RT64 compensates rectangles for horizontal expansion, but
raw triangles receive the full expansion. Without compensation the needle
stretches and its pivot moves away from the dial in wide windows.

The hook before `0x8002DAA4` corrects the completed triangle's three
signed 16-bit x coordinates at packet offsets `0x10`, `0x20`, and `0x30`.
They are quarter-framebuffer-pixel coordinates. It maps each x to
`center + (x - center + edge_travel) / width_scale`, where center is twice the active
descriptor's framebuffer width (`+8`) and edge travel is in quarter pixels.
Original placement uses zero travel. This matches the rectangle path,
including doubled horizontal-resolution modes and viewport-relative
coordinates. Y is not changed; in H-RES the builder stays on its normal
layout, as described above. Texture coordinates and shared source vertices
stay unchanged. The guest-memory contract and bounds are in
`game_camera.cpp`.

The ROM-backed camera test executes the actual needle builder with a captured
Hawaii needle fixture, checks final alignment after renderer expansion at
4:3, 16:9 and ultrawide, and checks both horizontal coordinate shifts. It also
checks that mode 6 keeps the fixture's Y once `wdc_hud_normal_shift` has run,
that mode 3 keeps the doubled Y, and that a clear vertical flag leaves Y
unchanged. This tests the generated hook as well
as its arithmetic.
