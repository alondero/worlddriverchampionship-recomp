# Render split prototype (throwaway)

Question: can the real race drawing code produce additional frames while input
sampling and simulation remain at their original fixed rate, and which state
must be separated before those frames can be interpolated?

This is a native experiment, not a shipping graphics option. It relinks an
already-built Windows/MinGW player with a small probe. The reference build and
generated code are read-only; no submodule or player source is patched. GNU
linker wrappers intercept the dispatcher, two sound-list boundaries, task
submission and BOSS output. Runtime counters check interception actually took
place. The executable, settings, logs and ROM-derived observations stay under
ignored `build/render-split-prototype/`.

## Run

Requires a completed Ninja/MinGW player build with its object files, static
libraries, dependencies and DLLs still present. Use the same compiler/toolchain
as that build. Supply your own validated USA ROM:

```powershell
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode split60
```

Repeatability gate and the frozen-memory regression (no player window for the
second command):

```powershell
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode repeat --require-repeatable
python tools/render_split_prototype/test_context.py --reference-build build --rom "World Driver Championship (USA).z64"
```

`--reference-build` may point to an existing build in another checkout. The
probe uses **that build's** game/runtime/renderer, not unbuilt source changes
in this worktree. It compiles only `probe.cpp` and relinks into a separate
executable. It does not perform a full player rebuild.

The run opens Hawaii practice at 1x resolution, waits for 60 eligible race
frames, performs six trials, drains the last extra graphics task through the
normal game path, then exits. The launcher kills the process after 60 seconds
if it does not finish. No controller input is required. It uses separate local
settings/save directories and does not load the user's normal saves.

| Mode | Experiment |
| --- | --- |
| `raw` | One additional original draw, including sound-list side effects |
| `sound-gated` | Skip sound-list rollover and consumption only on the extra draw |
| `repeat` | Two consecutive sound-gated extra draws without simulation between them |
| `camera-offset` | Sound-gated extra draw with camera eye X temporarily increased by one world unit; restore its three position words afterward |
| `split60` | Sound-gated extra draw and one-VI presentation waits for normal/extra draws; leave input sampling at 30 Hz |

Each mode writes `run.log`, `report.json`, `geometry.json`, `snapshots.json`
and `projections.json` under a fresh
`<mode>/run-*/` directory, retaining prior runs and starting with fresh settings
and Controller Pak storage. Run one probe process at a time. Reports
include sample counts, divider/tick pair, sound buffer selection, elapsed
times and all changed words in the first car record. The launcher rejects
missing trials, failed interception, changed sampling policy, simulation
sample advancement during extra draws and incomplete RDP evidence.
`--require-repeatable` additionally requires exact ordered triangle-header
equality against the normal draw, identical projection matrices, unchanged
camera words, first-car words except the four arena links, and an untouched
caller register context after **each** extra draw. This gate currently expects
one view. Snapshot observations are immutable host copies used for comparison;
the live experiment still draws in shared RDRAM and does not restore arenas.

## What was observed

The original extra-draw mismatch was a **prototype context-copy bug**.
`recomp_context::f_odd` points into its own floating-point register storage.
`auto draw_context = *ctx` left that pointer in the authoritative caller.
Translated odd-register accesses therefore read/wrote a different context
from even-register accesses. The projection changed with identical camera,
FOV and aspect inputs; the source context was also corrupted.
`context.h` now rebinds the pointer after every copy, matching librecomp's
`cop0_status_write` FR=0/FR=1 selection. Do not return such a rebound structure
by value and rely on copy elision.

Bounded Hawaii practice on Windows D3D12/RTX 3080, 1x resolution, divider 1 /
1,562,500 ticks:

- Before correction, six consecutive-extra pairs differed. One reproduced
  normal/extra/extra sequence was 414 / 540 / 355 triangles. Restoring all
  four view records between extras did not fix it (447 / 565 / 391).
- After correction, the strict gate passed all six normal/extra/extra groups:
  444 / 444 / 444, 422 / 422 / 422, 400 / 400 / 400, 424 / 424 / 424,
  438 / 438 / 438 and 432 / 432 / 432. Exact ordered header vectors and
  projection matrices matched, not merely their hashes. Both alternating
  geometry arenas were exercised without rewinding their cursors.
  A second fresh boot also passed all six groups (442, 436, 449, 464, 481,
  508 triangles respectively). Equality is within a frozen sample, not across
  boots that consume different numbers of input samples during warm-up.
- Every extra retained the camera's 32 words, first-car non-link words,
  caller context and consumed sample count. The four car list links differed
  on the first extra as the arena alternated, returning on the second extra.
  View zero `+304/+308` changed on the first extra and then stayed unchanged;
  this is still persistent render state, despite equal geometry.
- Corrected `split60` also emitted identical normal/extra headers in six
  trials, with 17.1–19.4 ms extra-draw intervals and stock 30 Hz sampling.
  These instrumented intervals are not measured physical scanout.
- Corrected `raw` and `sound-gated` runs each emitted identical normal/extra
  headers in six trials. Raw flipped the sound-list selector each time;
  gated retained it and intercepted both sound boundaries exactly once per
  extra. Both changed only the four first-car arena links.
- Corrected `camera-offset` changed triangle counts/headers in all six trials
  and 13 first-car words. Restoring camera position alone still leaves
  derived car state changed; this controlled perturbation is not interpolation.

`test_context.py` extracts the unchanged translated `80070DB0` projection,
`80071250`/`80071410` trig and `80077390` identity functions. Four executions
from the same frozen synthetic RDRAM image and register snapshot produce
bit-identical projection matrices and preserve the source context. A shallow
copy negative control must both corrupt the source and change the matrix.
The FR=1 pointer bank is checked separately; live game/projection testing is
FR=0. This is a frozen **projection** fixture, not an isolated full-frame
snapshot renderer.

Earlier geometry/car-difference conclusions made with the shallow copy are
superseded: they did not establish a ZSort ownership bug. Sound gating and
one-VI timing remain separate experiments, not proof of audio fidelity or
physical scanout.

## Ownership recovered

ROM reads with `tools/rom/inspect_code.py` and unchanged generated bodies
establish these boundaries (hex addresses/offsets):

| State | Producer and lifetime |
| --- | --- |
| Car `+B80/+B88`, associated `+B84/+B8C` | `28314` waits at `28344` for SP_BREAK, then `283C8..28428` finds signed lane-0 minima/maxima over four 16-byte transformed records and stores their associated signed `+E` lanes; `28448..2845C` may subtract 8 from the associated values. Derived view bounds, not car motion. |
| Car `+B90/+B94`, `+B98/+B9C` | Two geometry-list head/tail pairs. `284CC` initializes the first head to `10000000`; `28C04` saves its final tail. `28F10..28FA0` builds the second pair. `29D6C..29DD8` splices both into sort buckets. Addresses depend on the current arena; never interpolate or restore them independently of their lists. |
| Per-view record `800ABE40`, stride `1418` | `10A5C` clears its list via `F8BC` (`+68` head, `+6C` append link), builds the camera and transform requests, saves matrix `+1C..+5B`, normalization `+5C` and transform base `+18`. `10ECC` restores these values and walks the newly built list. |
| Matrix/transform arena `8007FE70`, counter `FE78` | `24038 -> 2D730` resets from `ED50 + ED54*8`; `2CF30` allocates 64-byte matrices downwards. The transform command stream grows upwards from `ED50`. |
| Geometry arena `8007FE74` | `24D38 -> 2D768` resets from `800B3830[ED94] + ED90*8`. `28314` and cache-transform helpers allocate/link packets. `24FCC` toggles `ED94` after submission; the original SP/DP/VI and SP_BREAK handshakes remain essential. |

`25410` reads indexed 16-byte transformed records from descriptor `+4`, tests
their clip bytes at `+C`, and copies their first 64 bits into newly allocated
triangle/quad packets (`254B8..25528`). `2703C` likewise indexes this transformed
buffer and tests clip bytes before constructing geometry. The CPU consumes
transformed results and builds new geometry; changing a matrix in an already
finished BOSS task is not a substitute for rerunning this pipeline.

The geometry collector tags each task at `osSpTaskStartGo_recomp`, consumes
tags FIFO at BOSS execution and hashes the first eight u32 words of each raw
RDP triangle (edges/coordinates plus opcode/tile/level fields, excluding shading
and texture coefficients).
It parses across RDP chunk boundaries, compares owned triangle-header vectors
against the normal draw, and excludes alternating framebuffer
addresses. Different hashes are evidence of different ordered triangle headers;
they can also differ due to material bits or command order. Different triangle
counts independently establish that the submitted streams differ. This is
**not proof of correct interpolation**. Neither identical pixels nor a
visual-quality comparison was measured.

## Limits and next experiment

The prototype does not interpolate cars or camera rotation, run above 60 Hz,
or isolate rendering from authoritative RDRAM. Camera offset is a controlled
perturbation, not interpolation. The six-trial, stationary practice route
does not establish gameplay equivalence, audio fidelity, sustained stability,
or split-screen/other-track behavior. One earlier instrumentation build
crashed; a debugger rerun did not reproduce it. Later bounded runs passed,
but that is not a stability qualification.

Input and graphics workers can run during swap waits. Car differences are
observations across the draw interval, not a complete attribution of every
write. Only the first car is inspected; the sample counter is not a full
simulation-state checksum. Audio synthesis and DMA remain in the original
combined BOSS submission even when sound-event processing is gated. Geometry
collection itself adds work, so these timings are not a performance benchmark.

Next: recover an explicit full-frame snapshot interface, including remaining
per-view/HUD history and all cars/effects. The corrected probe establishes
repeatable geometry for this frozen simulation sample, and the projection
fixture provides isolated frozen-memory coverage; neither establishes full
authoritative-state equivalence. Keep interpolation behind that ownership work.
Then separate the 60 Hz VI/AI clock from presentation scheduling to test
non-integer display ratios. Do not promote these wrappers into a shipping
option; recover explicit interfaces after the experiment establishes ownership.
