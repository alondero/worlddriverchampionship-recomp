# Render split prototype (throwaway)

Question: can the real race drawing code produce additional frames while input
sampling and simulation remain at their original fixed rate, and which state
must be separated before those frames can be interpolated?

This is a native experiment, not a shipping graphics option. It relinks an
already-built Windows/MinGW player with a small probe. The reference build and
generated code are read-only; no submodule or player source is patched in place. GNU
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
in this worktree. Ordinary modes compile only `probe.cpp` and relink into a separate
executable. It does not perform a full player rebuild.
The `present120` mode additionally compiles a copied `events.cpp` with guarded
substitutions from `runtime.py`, linked ahead of the reference runtime archive.
This copy uses the current source named by the reference compile database;
unbuilt changes to that file will therefore affect this mode.
Its per-run `runtime-source.json` records source/copy hashes and the compiler command.
The substitutions fail on unexpected source structure; other modes use the
unmodified runtime archive.
`shadow-snapshot` is a separate experiment: it compiles copied renderer and
runtime message-queue sources via `shadow.py`, replacing the renderer object
and queue archive member. `shadow-source.json` records both current-source and
copy hashes and compile commands; unbuilt reference-source edits affect this
mode. Dependency compiler warnings are retained in `shadow-build.log` under
the local prototype build directory. No reference file is edited.

The run opens Hawaii practice at 1x resolution, waits for 60 eligible race
frames, performs six trials, drains the last extra graphics task through the
normal game path, then exits. The launcher kills the process after 60 seconds
if it does not finish. No controller input is required. It uses separate local
settings/save directories and does not load the user's normal saves.
Use `--warp '1:0:arcade:3' --settle-frames 180` to audit the eight-car Hawaii
grid later in the race. `--warp` follows the player's existing race setup
syntax; `--settle-frames` accepts 60..1800 (increase `--timeout` as needed).
Each run retains `setup.json` alongside its evidence.

| Mode | Experiment |
| --- | --- |
| `raw` | One additional original draw, including sound-list side effects |
| `sound-gated` | Skip sound-list rollover and consumption only on the extra draw |
| `repeat` | Two consecutive sound-gated extra draws without simulation between them |
| `render-only` | Two extra draws also skipping audio command generation/retirement and preserving lens-flare screen-position history |
| `camera-snapshot` | Two render-only extras using a frozen 128-byte camera copy in owned transform-arena space; restore view/current-camera references after each draw |
| `shadow-snapshot` | Drain the native pipeline, capture an owned 8 MiB image, and rebuild each extra in a private RAM bank; CPU/BOSS/renderer use that bank while runtime queues stay authoritative |
| `present120` | Three render-only extras; pace swaps at 120 Hz and queue completed framebuffers directly to the graphics worker, retaining 60 Hz VI/AI events |
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
camera words, every active car's words except the four arena links, and an untouched
caller register context after **each** extra draw. This gate currently expects
one view. Snapshot observations are immutable host copies used for comparison;
all modes except `shadow-snapshot` still draw in shared RDRAM. None rewinds
live arenas; the owned bank is reset only after its pipeline has drained.
It supports `repeat`, `render-only`, `camera-snapshot`, `shadow-snapshot` and `present120`. The render-only variants additionally
reject view changes outside the lens-flare arena packet pointers. `present120`
also writes `presents.json`, counts actual renderer callbacks and VI ticks,
and requires 24 callbacks at an average 105–135 Hz with 9–14 VI ticks. These
broad bounds detect a remaining 60 Hz ceiling, not precise frame pacing.
`camera-snapshot` requires exactly one copy per extra and permits only the
expected 128-byte displacement of view zero's transform base/list links
(`+18/+68/+6C`), in addition to the previously established flare packet links.
`shadow-snapshot` additionally checks owned-memory task routing, one bootstrap
and one full-frame task per extra, and a live non-null message-receive sentinel
test. `--shadow-eye-offset 1` changes only the owned camera eye X: with the strict
gate, normal geometry must differ and both extras must match exactly, while
original camera/car state stays unchanged. This is a negative control, not
interpolation. `authority.json` records all changed authoritative word ranges
across each extra, including runtime queue and input activity; these ranges
are diagnostic evidence and are not yet a complete permitted-write policy.
`phases.json` separates transform setup, view passes, submission and swap/pacing
wall times. Geometry records also include BOSS task wall time and time spent
inside the downstream RDP sink. These overlap across CPU/graphics threads;
do not add them or interpret them as CPU-only costs.
`sync_chunk_sink_ms` counts sink calls whose accumulated command stream completes
a full-sync command. A chunk can also contain other commands, so this is an
upper bound on full-sync time, not an isolated function measurement.

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
- `render-only` passed all six normal/extra/extra groups (439, 444, 464, 473,
  493, 531 triangles), with audio build/retire skipped on every extra. The
  remaining per-view changes were only the newly allocated flare packet links.
- `present120 --require-repeatable` passed six normal/extra/extra/extra groups
  (444, 439, 436, 422, 418, 414 triangles). It delivered 24 renderer callbacks
  averaging 117.5 Hz (6.59–10.71 ms intervals), spanning 12 VI ticks. Consumed
  samples advanced once per group, 60 through 65, and never during extras.
  An earlier run averaged 119.8 Hz. This measures calls to `update_screen`,
  not physical scanout or new motion between the repeated frames.
- The later eight-car Arcade audit exposed additional sound work inside car
  rendering: `218F0 -> 1FC64` ramps car halfword `+A0E` by up to 20 per draw,
  and updates other sound history. Gating only the draw root's audio calls was
  insufficient. Render-only extras now skip `1FC64` and its distant-car
  retirement counterpart `1F7F8`. The same six-trial route then passed exact
  geometry and every active car's non-link words. It **failed** the timing
  gate at 62.0 renderer callbacks/s (10.77–23.53 ms intervals); sustained
  high-rate performance is not established by the lighter practice result.
- Timing instrumentation on the same later Arcade route measured 93.4 callbacks/s:
  mean extra-task time 7.69 ms, including 5.19 ms in the downstream RDP sink.
  Mean swap time was 0.60 ms, including 0.28 ms pacing/queue time; submission
  `24FCC` averaged 6.52 ms including its completion waits. Another run reached
  107.9 callbacks/s. The evidence points to variable rendering throughput,
  rather than a remaining fixed 60 Hz swap wait; it does not isolate the sink's
  CPU cost from its own worker waits or establish sustained 120 Hz performance.
  A follow-up measured 5.34 ms sink time, of which 3.62 ms occurred in chunks
  completing full-sync commands. The renderer's full-sync path includes workload
  preparation and uploader/worker waits; removing the game's swap waits cannot
  eliminate that cost.
- `camera-snapshot --require-repeatable --warp '1:0:arcade:3' --settle-frames 180`
  passed six eight-car groups (1252, 1260, 1256, 1266, 1270, 1265 triangles,
  each repeated three times). The original camera, every car's non-link words,
  caller registers and consumed sample count remained unchanged after each
  extra. This isolates the camera input/output record, not the entire frame.
- `shadow-snapshot` passed six later eight-car groups with identical normal
  and owned-RAM extra geometry. A separate eye-X +1 negative control changed
  every normal/extra geometry pair while the two extras matched exactly.
  Original camera/car words, caller context and consumed sample counts stayed
  unchanged. The live receive test left an authoritative sentinel intact and
  delivered the message into the corresponding owned address.

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
| Camera snapshot | `11D54` reads the camera basis/position and projection fields, then writes derived `+7C` at `11DCC`. The experiment reserves 128 bytes below `FE70` after the normal arena reset, checks collision against `ED40`, copies the frozen host words and redirects view `+2DC` through both passes. It restores only that reference and current-camera global `ED6C`; the arena is not rewound. |
| Lens-flare object, view `+2F4` | `2A3A8` shifts four screen-position history words at object `+10..+1C`; render-only extras preserve these around the call. Object `+6C/+70` are newly allocated packet pointers written at `2A280/2A284`, so view `+360/+364` must retain the current arena addresses. |
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

The prototype does not interpolate cars or camera rotation. The shadow mode
uses owned render RAM; other modes still borrow authoritative RDRAM (the camera
copy mode isolates only that record). Camera offset is a controlled
perturbation, not interpolation. The six-trial, stationary practice route
does not establish gameplay equivalence, audio fidelity, sustained stability,
or split-screen/other-track behavior. One earlier instrumentation build
crashed; a debugger rerun did not reproduce it. Later bounded runs passed,
but that is not a stability qualification.

Input and graphics workers can run during swap waits. Car differences are
observations across the draw interval, not a complete attribution of every
write. Every active car is inspected, but the sample counter and car records
are not a full simulation-state checksum. Audio synthesis and DMA remain in the original
combined BOSS submission except on render-only variants' extras. Geometry
collection itself adds work, so these timings are not a performance benchmark.

The owned-RAM handoff uses `252D0`: it writes an empty next-transform batch,
waits through the original SP/DP/swap path and clears `EDB8`. Merely waiting
for task completion deadlocks because a BOSS task awaits the next transform
batch. A fresh bank starts via the normal `2523C` bootstrap. The copied renderer
selects task memory for both BOSS and RT64's core/state RAM pointers. A mutex
serializes bank refresh with renderer task processing and VI screen updates;
completion is signalled only after held RDP chunks and task cleanup finish.
`osSendMesg`/`osRecvMesg` use authoritative queue/thread structures. A thread-local
receive-store adapter writes a non-null result directly into the owned bank,
without first touching the corresponding authoritative address. The image is
never copied back into authoritative RAM.

Next: classify remaining authoritative changed ranges, qualify the snapshot
handoff across transitions and combine it with independent presentation.
The bounded owned-RAM geometry proof and projection fixture do not establish
full gameplay/audio equivalence or snapshot behavior in every scene. The
drain/bootstrap experiment retains stock VI waits and is not a high-rate
scheduler. Interpolation, rotations, discontinuities and effect handling still
need explicit policies before this can become a shipping option.
The direct-presentation experiment is bounded to progressive single-view racing;
it skips only swap routine `013DC` receives on VI queue `800AAA28`, retaining
SP_BREAK and DP completion waits. Its copied runtime locks VI state transitions
and queues owned VI register values on the existing serialized graphics queue.
Pacing services external messages, allowing input/audio workers to run. It
still needs non-integer ratios, transition handling and sustained validation.
Do not promote these wrappers into a shipping
option; recover explicit interfaces after the experiment establishes ownership.
