# Render split prototype (throwaway)

Question: can the real race drawing code produce additional frames while input
sampling and simulation remain at their original fixed rate, and which state
must be separated before those frames can be interpolated?

`--async-diagnostics` moves complete probe diagnostic records to a host writer
thread while retaining the existing record-formatting mutex. This isolates file
I/O from that mutex; it does not eliminate formatting or scheduling contention.
The queue and writer batch each hold at most 4 MiB of payload (string allocation
capacity/overhead is additional). Producers wait when the queue is full; records
are never intentionally dropped. The writer takes no rendering or output mutex.
Exits through `probe.cpp` drain and join the writer, report record/byte totals and
backpressure/write timing in `diagnostics.json`, then flush stderr. Rejected
records, sink failures or flush failures exit with code 29. The runner requires
one successful nonempty drain audit with matching accepted/written counts.
Immediate aborts in existing bank/sampler/hash helpers, runtime crashes and
forced termination can bypass draining; such runs cannot pass this audit.
Available drain records are saved even when the native process exits nonzero. Queueing can reorder probe records relative to
other runtime logs; selected-frame joins still use IDs and captured timestamps.
This is opt-in diagnostic plumbing, not a change to presentation acceptance.

Check the writer's ordering, bounded backpressure, final drain and failure paths:
`python tools/render_split_prototype/test_async_log.py --reference-build build`.

Owned-task audio isolation is checked at the BOSS DMA boundary as well as the
CPU command helpers. `geometry.json` records rounded DMA bytes and overlap with
the 16 KiB PCM ring at physical `3F0400..3F4400` for every observed task, including
bootstrap tasks. Owned tasks must write zero ring bytes; full-frame tasks must
perform nonzero DMA so a missing observer cannot silently pass. The observer
forwards every transfer unchanged on the graphics worker. This measures ring
writes, not synthesis instruction time or audio fidelity; absence of ring
writes alone does not prove that every audio-related instruction was skipped.

`--capture-boss-tasks` is a diagnostic fixture for six-trial `pose-snapshot`
runs. It arms the existing BOSS recorder immediately before the first owned
draw's bootstrap and full task, on the graphics worker without SDL calls.
Existing write journalling and completion-signal handling remain intact.
Both local `.boss` files must pass the reference `wdc_boss_bench.exe` exact RDP
replay check before the runner accepts the run; `boss-captures.json` records
capture and benchmark hashes. Capture overhead makes these runs unsuitable for
timing claims. The reference build must already contain the benchmark.

For an offline code-generation comparison, run
`python tools/render_split_prototype/benchmark_boss_avx2.py --reference-build build --capture <owned-task-1.boss>`.
This compiles an unchanged copy of generated BOSS with `-mavx2`, checks host/OS
AVX2 support, and brackets its replay with two reference-benchmark controls.
It never replaces the player object or changes generated/reference source.
Artifacts and provenance stay in a unique ignored
`build/render-split-prototype/boss-avx2/run-*` directory with explicit run status.
Every replay must reproduce the captured RDP stream exactly. Timing excludes
RT64, live CPU handshakes and RDRAM reset; other DMA output is not compared by
the existing benchmark. A faster offline replay alone does not qualify live
performance, pixel equivalence or audio fidelity.

`--sample-render-worker` is a Windows-only diagnostic for owned-RAM modes.
It requests instruction-pointer samples about every 250 microseconds while
owned BOSS tasks and their synchronous RT64 sink execute. A helper thread
briefly suspends the graphics worker; it resumes that worker before allocating,
aggregating, logging or acquiring application locks. The game thread stops and
joins the collector only after the existing renderer-completion fence.
Setup, timer, resume, output or zero-sample failures abort with exit 28.

`worker.samples` records executable-relative instruction offsets plus counts
outside the executable and failed observations. The run retains the exact
`sampled-renderer.exe`, hashes and `worker-profile.txt`, using the existing BOSS
symbolication tool. Reference RT64 objects may lack line information; function
and inlining attribution is approximate. `worker-functions.txt` adds nearest
text-symbol intervals for samples without DWARF function information and marks
those rows explicitly; symbol intervals can include padding and do not recover
inline call stacks. DLL code and blocked waits can fall
outside the executable, so percentages in the symbol report describe only
in-image samples, not total task time. This is a sampled instruction pointer,
not a call-stack profile. Suspension perturbs scheduling and timing; all swap
and timing gates remain enabled, but a sampled run is not performance acceptance.

This is a native experiment, not a shipping graphics option. It relinks an
already-built Windows/MinGW player with a small probe. The reference build and
generated code are read-only; no submodule or player source is patched in place. GNU
linker wrappers intercept the dispatcher, two sound-list boundaries, task
submission and BOSS output. Runtime counters check interception actually took
place. The executable, settings, logs and ROM-derived observations stay under
ignored `build/render-split-prototype/`.

**Presentation audit:** callback cadence does not establish displayed cadence.
The opt-in `--audit-present` check currently exposes missing presentations:
a 30-group 144 Hz practice run with owned readback disabled produced 143
callbacks but only 30 RT64 swap calls. The extras in each group reused one
presentation ID. RT64 `State::updateScreen` only queues a present when VI or
framebuffer RAM changes (or its early-present path is used); new GPU contents
alone do not satisfy that condition. With readback enabled, a separate run
produced 142 callbacks and 139 swaps: three identical initial poses were still
deduplicated. These are failed audit controls, not high-refresh acceptance.
The optional explicit-submission experiment below addresses this omission.

`--audit-present` requires an owned-RAM independent-presentation mode. It builds
a copied RT64 present-queue object and records `present-audit-source.json`,
`present-bindings.json` and `swaps.json`. Each callback must increment RT64's
presentation ID and match exactly one successful, ordered swap call. Startup
swaps are retained but excluded from the selected sequence. Final shutdown
waits up to ten seconds for the last bound present event's entire swap loop,
not merely its first swap. This final diagnostic drain does not pace individual
frames. The audit adds logging overhead; swap completion still does not measure
physical scanout or prove pixel identity. Image capture is a separate option.

The copied queue also records `presentation-skips.json`: skipped event IDs and
whether a newer queued event or an empty swapchain caused the skip. These
records include startup and are retained after a nonzero native exit. A skipped
event never counts as a successful swap or advances the final audit fence.
Final audit timeouts log the last bound and last finished event IDs. Explicit
submission prevents enqueue deduplication, but RT64 can still skip queued
events when the presentation worker falls behind. The strict swap audit rejects
any resulting loss in the selected sequence.

Successful swap audits also write `swap-timing.json`: duration, rate and
minimum/median/p95/p99/maximum intervals, using nearest-rank percentiles over
intervals rather than events. Counts below 0.75 target periods and above 1.5
periods expose short bursts and long gaps even when average rate passes. These
thresholds are diagnostics, not a new smoothness acceptance gate. Existing
rate/VI checks remain unchanged. Long runs with held accelerator still use
neutral steering and can hit a wall; they are not full-playthrough evidence.

Audited runs also write `present-latency.json`, joining each selected swap to
same-clock observations at prototype screen-callback entry, present dequeue
observation, workload readiness and swap-API entry/return. Each worker's phase
order is checked, but the worker may dequeue or finish before the callback's
binding observer returns. Screen-to-dequeue includes admission, enqueue work,
queueing and pre-observation resize work; dequeue-to-ready includes any injected
dequeue delay and workload waiting. Ready-to-API includes presentation rendering,
waits and pacing. API duration includes driver time and thread scheduling.
These are elapsed phase spans, not CPU time or precise queue-wait measurements.
No new wait, completion signal, skip decision or timing acceptance rule is added.

`--pace-present-queue --explicit-present --audit-present` experiments with
admission pacing on the graphics worker. Before each prototype screen update,
it waits until the present worker has dequeued the previously bound event and
made its backlog decision. This allows one waiting event alongside the active
presentation. It does not wait for swap completion. A mutex/condition variable
protects the dequeue predicate; a ten-second timeout aborts with exit 26.
The present worker signals before waiting on the event's workload, and the
graphics worker waits before entering `updateScreen`, with no probe locks held.
VI/AI delivery and the game thread's external-message drain remain unchanged.

`present-admissions.json` records the predecessor, observed dequeue ID and wait
duration for every selected callback; missing or premature admissions fail.
Startup events are outside this admission policy, so the first selected frame
still relies on the swap audit to detect transition loss. Empty swapchains and
unsuccessful swaps remain failures. The option cannot be combined with image
capture's separate completion serialization. Timing acceptance remains enabled;
preventing queue loss alone does not establish sustained high refresh.

For a bounded negative control, `--present-dequeue-delay-ms 40` stalls the
present worker for 40 ms between its dequeue decision and notification. It
starts after the first selected event is bound, outside probe locks. Values
are limited to 0..100 ms, explicit presentation and exactly six trials without
capture. Compare runs with and without `--pace-present-queue` to exercise queue
backpressure. `setup.json` records the injected delay; swap and timing gates
are still enforced, so an intentionally slow run is expected to fail timing.

`--capture-frames --explicit-present --audit-present` captures final swap-chain
RGB images after VI/UI rendering, before presentation. It requires exactly six
trials. The copied present queue reads back into a local GPU buffer after the
existing completion fence and writes `present-ID-0.ppm`; `captures.json` links
each selected presentation to its image dimensions and RGB SHA-256. Captures
and manifests stay in the ignored run directory. Conversion from the pinned
BGRA8 swap-chain format and D3D12's 256-byte row pitch is explicit; application
and backend source hashes are recorded. The pinned backend assumes a texture
destination even for buffer copies, so the diagnostic supplies the single-sample
swap texture to its sample-position helper while the footprint still selects
the readback buffer. Reference dependencies remain unmodified.

Capture-only serialization waits for each entire present event before releasing
its graphics callback; otherwise slow file output can cause RT64 to discard
queued presents. One un-serialized control omitted two of 15 requested images,
which the strict audit rejected. Capture runs retain the state/geometry/queue
and 60 Hz VI checks but explicitly omit target-rate acceptance; their timings
are diagnostic only. They cannot qualify normal scheduling or performance.
The runner requires every selected image and exact frozen normal/extra pixel
equality in frozen modes without an intentional eye offset. A serialized 75 Hz frozen control captured all 15
1280x720 images across six groups, with identical pixels within every group.
Visual inspection of its first captured frame showed a coherent car, track and
HUD. This is bounded pixel-repeatability evidence, not general visual quality
or physical-scanout qualification.
A combined camera/body/wheel/shadow 75 Hz capture also passed all retained
checks and wrote 15 images. The initial identical-pose group had identical
pixels; all five subsequent groups had distinct images for each selected pose.
Inspection of the first and last poses in the last group showed coherent car,
wheel, track and HUD imagery as the camera moved. These captures show the
stationary pre-race camera sequence, not driving or high-speed wheel-spin
qualification. Capture-induced sample gaps also make this unsuitable for
judging chronological smoothness.

Use `--explicit-present --audit-present` to submit every prototype screen
update even when RT64 sees unchanged VI/framebuffer RAM. The copied state
object adds a graphics-thread-local request to `State::updateScreen`'s enqueue
condition; ordinary VI callbacks retain deduplication. It preserves VI history,
framebuffer handling, workload/present IDs and queue fences. The audit and all
retained state/geometry checks are mandatory, and the measured swap rate must
pass the target timing band as well as the callback rate. This option can be
combined with `--profile-rt64`; `rt64-profile-source.json` records both flags,
even when profiling is disabled and only explicit submission is enabled.

A 30-group 144 Hz practice run with wheel/shadow interpolation and sample/deadline
scheduling produced 67 selected frames and 67 successful ordered swaps. All
retained correctness checks passed, but timing failed at 15.5 swaps/s with
18.32..206.84 ms intervals. This establishes that the requested frames reach
the swap API, not high-refresh performance or pixel identity. The timing of
earlier runs that silently omitted swaps is not a valid performance baseline.

A six-group frozen 144 Hz control with profiling matched 28 callbacks to 28
swaps at 98.1 swaps/s (0.96..25.53 ms), also failing timing. Mean full sync was
4.735 ms, including 3.956 ms framebuffer GPU wait. A separate 30-group combined
interpolation run at 75 Hz passed all retained gates with 75 callbacks and 75
swaps at 75.0 swaps/s (5.67..35.10 ms); callbacks averaged 74.9/s over 60 VI
ticks. All three runs disabled owned readback and whole-RAM scans. Short-run
variation and long intervals still prevent sustained performance or smoothness
claims. Pixel/readback equivalence remains unverified.

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
Independent presentation modes use the reference runtime's compiled swap
presentation API when available (patch 0022). The graphics worker receives an
immutable VI register snapshot for each selected swap. Guest current/next
framebuffer state still latches on retrace. The VI thread also retains ownership
of VI/AI message delivery. Normal player startup uses retrace-driven updates.
The per-run `runtime-source.json` identifies the linked runtime archive by hash.
Older reference builds compile a separate copy of `events.cpp` using the
reference compile database and the guarded substitutions in `runtime.py`.
Those runs record the source hash, copy hash and compiler command.
Owned-RAM modes use the compiled player renderer when it exposes a frame
source. At renderer creation the probe supplies task memory, a snapshot mutex,
a task-completion callback and an optional screen-update observer. The renderer
binds BOSS and RT64 to the same bank, retains it through screen update and
restores the task's readback policy before reporting completion. The probe
retains the bank until both task and requested screen completions arrive.
Normal player startup uses live RDRAM. Runtime patch 0021 separately supplies
the queue receive destination. `shadow-source.json` identifies both compiled
components and their hashes. Older reference builds compile a renderer or
queue copy for each unavailable boundary and record source/copy provenance.

The run opens Hawaii practice at 1x resolution, waits for 60 eligible race
frames, performs six trials, drains the last extra graphics task through the
normal game path, then exits. The launcher kills the process after 60 seconds
if it does not finish. No controller input is required. It uses separate local
settings/save directories and does not load the user's normal saves.
Use `--warp '1:0:arcade:3' --settle-frames 180` to audit the eight-car Hawaii
grid later in the race. `--warp` follows the player's existing race setup
syntax; `--settle-frames` accepts 60..1800 (increase `--timeout` as needed).
Each run retains `setup.json` alongside its evidence.
`--hold-accelerator` is an opt-in driving fixture: after ordinary controller
sampling, it holds port-0 A and neutralizes both sticks only for the known pad
array during the live race, outside replay. It preserves other buttons/ports.
`inputs.json` records cumulative injected samples per trial and the runner
requires advancing counts. This changes sampled input, not the simulation clock
or extra-draw lifecycle. The current port-0 callback always reports success;
the guest error flag alone would not establish a fresh host response if that
runtime behavior changed. The fixture does not steer around corners or qualify
controller disconnect behavior.

Use a later race boundary, such as `--warp '1:0:arcade:3' --settle-frames 240`,
with this fixture to investigate driving. A captured run showed 37 mph and an
active race timer. Without the fixture, a later 360-frame capture still showed
0 mph. Short default captures cover the pre-race camera and cannot establish
driving quality.
Use `--no-authority-scan` in owned-RAM modes to isolate the cost of whole-RAM
diagnostics. This timing experiment omits `authority.json` observations (the file
is an empty array), records `authority_scan: false` in `setup.json`, and retains
the camera/car/context/sample, pose, queue and task-owner gates. It does not
provide full-RAM changed-range evidence. The default still scans every extra.

Use `--trials 120` for a short timing run or `--trials 1800 --timeout 180`
for roughly one minute at the nominal 30 groups/s (6..1800, default 6); increase
`--timeout` for workloads that need more than 60 seconds including startup.

| Mode | Experiment |
| --- | --- |
| `raw` | One additional original draw, including sound-list side effects |
| `sound-gated` | Skip sound-list rollover and consumption only on the extra draw |
| `repeat` | Two consecutive sound-gated extra draws without simulation between them |
| `render-only` | Two extra draws also skipping audio command generation/retirement and preserving lens-flare screen-position history |
| `camera-snapshot` | Two render-only extras using a frozen 128-byte camera copy in owned transform-arena space; restore view/current-camera references after each draw |
| `motion-samples` | Camera/body interpolation across actual consumed-sample spans, bounded to recent presentation slots |
| `motion-rate` | Owned camera/body interpolation on a rational display grid; normal presentation suppressed |
| `shadow-rate` | Frozen owned-RAM repeatability with rational 60..240 Hz presentation counts and pacing; no interpolation |
| `shadow-snapshot` | Drain the native pipeline, capture an owned 8 MiB image, and rebuild each extra in a private RAM bank; CPU/BOSS/renderer use that bank while runtime queues stay authoritative |
| `present120` | Three render-only extras; pace swaps at 120 Hz and queue completed framebuffers directly to the graphics worker, retaining 60 Hz VI/AI events |
| `shadow120` | Combine owned RAM with independent presentation; three frozen extras and a native drain per frame, with both copied-source provenance records |
| `endpoint120` | At the original draw call, present three owned blends followed by the original authoritative draw/audio as the current endpoint; remove the redundant owned endpoint |
| `motion120` | Suppress normal-draw presentation and present four owned camera/body blends at alpha 0.25, 0.5, 0.75 and 1; validate callback order and current endpoint |
| `pose-snapshot` | Interpolate owned camera and car-body poses between captured samples; render the midpoint twice and current endpoint once, with mandatory geometry/state checks |
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
Owned-RAM modes are `shadow-snapshot`, `shadow120`, `pose-snapshot`, `motion120` and `endpoint120`;
other modes still draw in shared RDRAM. None rewinds
live arenas; the owned bank is reset only after its pipeline has drained.
It supports `repeat`, `render-only`, `camera-snapshot`, `shadow-snapshot`, `shadow120` and `present120`. The render-only variants additionally
reject view changes outside the lens-flare arena packet pointers. Independent-presentation modes
write `presents.json`, count actual renderer callbacks and VI ticks,
and require four callbacks per trial at an average 105–135 Hz. The VI count
must be within two ticks of 60 Hz over the measured callback interval. These
broad bounds detect a remaining 60 Hz ceiling, not precise frame pacing.
`camera-snapshot` requires exactly one copy per extra and permits only the
expected 128-byte displacement of view zero's transform base/list links
(`+18/+68/+6C`), in addition to the previously established flare packet links.
Owned-RAM modes additionally check owned-memory task routing, one bootstrap
and one full-frame task per extra, and a live non-null message-receive sentinel
test. `--shadow-eye-offset 1` changes only the owned camera eye X: with the strict
gate, normal geometry must differ and all extras must match exactly, while
original camera/car state stays unchanged. This is a negative control, not
interpolation. `authority.json` records all changed authoritative word ranges
across each extra, including runtime queue and input activity; these ranges
are diagnostic evidence and are not yet a complete permitted-write policy.
`phases.json` separates transform setup, view passes, submission and swap/pacing
wall times, plus owned-bank reset, pipeline drain and full-RAM diagnostic scan.
Reset subphases separate waiting for the renderer mutex, copying the bank and
preparing interpolated inputs. Their timestamps precede diagnostic output;
input preparation includes its existing pose log, and the enclosing reset
timer includes all reset logs. The three subphases are nested within reset,
not additional independent work. Generic phase timers (including `owned_capture`)
and `pace_and_queue` include acquiring the diagnostic output mutex before their
end timestamp; a long span alone does not attribute the delay to memory copying
or rendering. The async writer isolates file I/O but retains this mutex.
`owned_pipeline_drain` measures the original `252D0` call;
`owned_renderer_fence` measures the subsequent wait for task cleanup and queued
screen callbacks. These intervals are nested within `owned_drain`; their logs
are emitted after both measurements.
The scan skips byte-identical 4 KiB pages and batches its output while retaining
the complete contiguous changed-word ranges. Geometry records also include BOSS task wall time and time spent
inside the downstream RDP sink. These overlap across CPU/graphics threads;
do not add them or interpret them as CPU-only costs.
`sync_chunk_sink_ms` counts sink calls whose accumulated command stream completes
a full-sync command. A chunk can also contain other commands, so this is an
upper bound on full-sync time, not an isolated function measurement.
Geometry records also count BOSS COP0 status reads and consecutive equal status
values per task. A linker wrapper delegates every read to the original bridge,
including its yield and capture/replay handling, then updates graphics-thread
counters. Owned modes require observations for every complete full-frame task.
Equal values do not distinguish busy waiting from separate status checks in
useful work, and these counters do not measure time spent yielding.

## What was observed

The original extra-draw mismatch was a **prototype context-copy bug**.
`recomp_context::f_odd` points into its own floating-point register storage.
`auto draw_context = *ctx` left that pointer in the authoritative caller.
Translated odd-register accesses therefore read/wrote a different context
from even-register accesses. The projection changed with identical camera,
FOV and aspect inputs; the source context was also corrupted.
`src/guest_context.h` rebinds the pointer after every copy, matching librecomp's
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

## Pose interpolation validation

```powershell
python tools/render_split_prototype/test_pose.py --reference-build build
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode pose-snapshot --warp '1:0:arcade:3' --settle-frames 180
```

`pose-snapshot` always enables strict checks. The camera and each active car's
main-body matrix are copied into immutable host records after normal graphics
drain. `pose.h` interpolates translation linearly and rotation via shortest-arc
quaternions, retaining opaque padding and bit-exact endpoints. It rejects
non-finite, scaled, sheared or reflected bases outside its explicit tolerance;
invalid poses abort the probe. Host tests cover rotations about all three axes,
180-degree and wraparound cases, translation, padding and invalid inputs.

The first pair starts with identical endpoints. History resets on scene,
camera pointer/mode, car identity/count changes, zero or more than four elapsed
samples, or a translation jump above 64 world units. These are provisional
bounds, not complete camera-cut/save-state/respawn detection. Rotational cuts,
projection changes and alternate views still need explicit policies.

The three extras use alpha 0.5, 0.5 and 1.0. Both midpoint geometry streams must
match exactly; the endpoint must reproduce normal geometry. At least one
midpoint must change a pose and geometry during the run. Existing original
camera/car/context/sample, queue, arena and sound gates remain mandatory.
`poses.json` records reset status and counts changed camera/body poses per draw.
This proves an intermediate rigid-pose input reaches geometry construction;
it does not prove complete animated-car consistency, smooth display order or
pixel quality. Detailed run evidence stays in the draft PR.

## Ordered motion presentation

```powershell
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode motion120
```

`motion120` combines the rigid-pose math with independent presentation. The
normal draw still supplies current-state geometry evidence and original audio
work, but its `osViSwapBuffer` call is suppressed during the experiment. Four
owned draws then present alpha 0.25, 0.5, 0.75 and 1.0. Each bank remains alive
until its task and queued screen update finish. This incurs interpolation
latency and extra normal-draw work; it is not yet an arbitrary-rate scheduler.

Strict state/ownership and endpoint checks are mandatory. `presents.json`
includes frame/draw labels attached before enqueue and consumed at the actual
renderer callback. The launcher requires exactly the four owned draws per
trial, in order, with no normal draw between them. These are renderer callback
observations, not physical scanout or visual-quality validation. The normal
swap's omitted framebuffer update can take the extra VI receive at `015E8`;
the gate allows that documented optional receive in addition to the ten
ordinary receives per trial (normal drain plus four owned drains).

The geometry-repeat mode remains separate: consecutive quarter steps are
expected to differ. The alpha-1 endpoint must still match normal geometry.
Wheel/shadow/effect interpolation, robust cut handling, arbitrary display rates,
and sustained performance remain incomplete; run evidence is in draft PR #61.

## Reusing the original endpoint draw

```powershell
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode endpoint120 --no-owned-readback
```

`endpoint120` runs its snapshot experiment at the dispatcher's original
`8000C870 -> 8000A30C` call. It presents three owned blends at 0.25, 0.5 and
0.75, then executes and presents the original authoritative draw at the current
endpoint. The normal audio path and original post-draw dispatcher calls remain
in order. Four full-frame tasks replace `motion120`'s five. The readback flag is
optional and retains the limitations described below.

`motion-samples --reuse-endpoints` uses the same dispatcher boundary with the
consumed-sample scheduler. When the end of a snapshot interval is exactly on
the display grid, the normal draw supplies that slot after the owned blends.
Otherwise the normal draw runs its audio and game bookkeeping with presentation
suppressed. For a 144 Hz target and consecutive 30 Hz samples, every fifth
sample endpoint is eligible. Sample gaps and history resets use the actual
interval end. The runner independently derives eligible endpoints from the
rate and sample counters, checks presentation order and requires exactly one
authoritative task pair and audio lifecycle per group. With deadline skipping,
the final owned blend is retained before any scheduled native endpoint.

Two 120-group track 2 driving runs at a 120 Hz target delivered all 480
selected swaps. Reusing the native endpoint in all 120 groups gave 114.9
swaps/s and a 12.43 ms p95 interval. Drawing the endpoints again in owned
memory gave 99.8 swaps/s and 19.12 ms p95. The median interval across group
boundaries was 9.87 ms with reuse and 17.51 ms in the control. The reuse run
also had a 48.41 ms maximum interval. An earlier control failed its scripted
input gate and is excluded. These timings use software swap timestamps from
separate runs on the same host. At 144 Hz a separate 30-group run reused five
endpoints, preserved all 141 selected swaps and failed timing at 100.1 swaps/s.
That run used the original texture hasher.

Both 120 Hz runs used `--mode motion-samples --present-hz 120 --warp
'2:0:arcade:3' --settle-frames 240 --hold-accelerator --wheel-poses
--shadow-inputs --audit-present --explicit-present --skip-late-draws
--write-watch-reset --no-authority-scan --no-owned-readback
--notify-renderer-completion --pace-present-queue --fast-texture-hash
--batch-texture-hash --trials 120`. Add `--reuse-endpoints` for the native
endpoint run. The swap audit records within-group and between-group screen
and swap intervals in `swap-timing.json`.

Current player builds expose `wdc_render_race_draw` at the dispatcher's
original draw instruction. The port passes the live context to the ROM draw.
The endpoint harness uses the same compiled dispatcher and wraps that external
call at link time. `draw-boundary-source.json` records the compiled archive
hash and boundary symbol.

For older reference players the dispatcher and draw root share an object.
When the archive lacks the port call, `draw_boundary.py` compiles a local copy
of the older reference's `funcs_3.c` with that call redirected. Its evidence
records both source hashes and the compiler command.

The live caller context and stack remain in scope until the original draw
returns. Extra draws and native drains use correctly rebound context copies.
The snapshot is captured before the normal draw, so immutable original-state
observations cover only the three owned draws. The authoritative endpoint is
expected to perform its normal state writes. `endpoints.json` requires one
original sound-list publish/consume and audio build/retire per group, with no
simulation samples consumed across the drawing interval. This is lifecycle
coverage, not audio-fidelity proof.

Task gates require three owned bootstrap/full-frame pairs and one authoritative
pair. Callback order must be `1,2,3,0`: the endpoint is the original draw itself,
not a second rendering compared against it. The separate `pose-snapshot` mode
retains the independent endpoint equality check. The first group's pipeline
drain can add two VI receives; subsequent groups require eight mandatory
receives plus at most the documented optional framebuffer receive. Per-trial
`report.json` timings stop before the original endpoint, while callback timing
spans the full presented sequence.

A 30-group held-accelerator Arcade run with wheel/shadow interpolation, explicit
audited presentation, RT64 profiling and owned readback/full-RAM scans disabled
retained all 120 ordered swaps and passed state, pose and authoritative audio
lifecycle checks. It reached 89.0 swaps/s (6.66..49.77 ms intervals), failing the
120 Hz timing gate. A repeat with status-read counters retained all 120 swaps
at 99.2/s (2.10..22.59 ms), still failing timing. Its 90 full owned tasks averaged
1458.7 status reads and 1456.3 consecutive equal values; the 30 authoritative
tasks averaged 1879.1 reads and 1877.1 equal values. Owned tasks averaged 6.343 ms,
including 4.320 ms in the RDP sink. These observations motivate investigating
the bridge's yield on every status read; they do not establish that polling is
the bottleneck or that changing its policy is safe. Shared-host timing variation
prevents attributing the rate difference to the counters. General driving
visual/audio quality and sustained 120 Hz delivery remain unqualified.
A copied-bridge experiment yielded on the first owned status read and every
64th thereafter, pausing on other reads while preserving every atomic read and
all normal-task yields. Policy counters verified 23 yields per owned task
instead of about 1459. All 120 swaps and retained correctness checks passed,
but delivery remained 92.7/s and owned task time was 6.340 ms versus 6.343 ms
in the prior control (RDP sink 4.327 versus 4.320 ms). The experiment was removed:
this workload provided no measured benefit from reducing yield frequency.

### Renderer completion notification experiment

`--notify-renderer-completion` is opt-in for owned-RAM modes and forces strict
checks. Task cleanup and screen-callback completion still both have to reach
their submitted/requested counts before bank reuse. The game thread services
one pending external runtime message without blocking, then waits on a host
condition variable. Completion publishers increment their existing atomic
counters under the same mutex used by the wait predicate, then notify the
waiter. This prevents a missed completion wake. Runtime message processing
occurs outside that mutex, so it cannot hold the completion lock across guest
scheduling. The original polling path remains the default.

External runtime messages do not notify this condition variable. A nominal
one-millisecond timeout keeps servicing them while completion remains pending;
this is not an identical message-delivery schedule or a hard latency bound.
VI/AI clocks and renderer memory locking are unchanged. The option does not
relax task/callback readiness or wait for physical scanout.

A 30-group driving endpoint control measured 6.799 ms in pipeline drain and
0.898 ms in the following renderer fence, at 99.7 successful ordered swaps/s.
Notification reduced the fence to 0.101 ms and retained all 120 swaps and
correctness checks, but its 104.7 callbacks/s still failed the broad 120 Hz
timing band. A 120-group notification run retained all 480 swaps and passed
that broad band at 107.7 swaps/s (3.66..49.67 ms intervals) and 107.8 callbacks/s.
Its fence averaged 0.093 ms and pipeline drain 6.799 ms. This is reduced waiting,
not steady 120 Hz delivery or general visual/audio qualification. All three
runs used held accelerator, wheel/shadow interpolation, fast texture hashing,
explicit audited presentation and RT64 profiling, with owned readback and
full-RAM diagnostics disabled.
A frozen driving capture with notifications enabled then retained all 24
images and ordered swaps across six groups. Normal and extra triangle headers
and per-group RGB hashes matched exactly; full-RAM diagnostics were enabled.
Capture serialization makes its timing diagnostic and does not qualify the
non-capture scheduling path.
Two 120-group `motion-samples` runs at a 144 Hz target passed retained correctness
and swap-order checks but failed timing. With notifications, all 492 selected
swaps arrived at 66.9/s (4.04..115.51 ms intervals); the fence averaged 0.357 ms,
reset 1.756 ms and pipeline drain 7.499 ms. The polling control retained all
543 selected swaps at 86.9/s (6.67..56.44 ms); its fence averaged 0.958 ms, reset
1.035 ms and pipeline drain 6.063 ms. Both used the same requested features,
including deadline skipping, wheel/shadow interpolation and fast hashing.
The notification run was slower overall despite its lower average fence cost;
the different selected workloads and broader timing spikes prevent attributing
the difference to a single cause. A regression in this path is not ruled out.
Keep this option experimental; it does not qualify general high-refresh pacing.

### Write-tracked bank reset experiment

`--write-watch-reset` uses a Windows `MEM_WRITE_WATCH` allocation for the owned
8 MiB bank and keeps its frozen snapshot in the same storage owner. Initial
capture and restore copy all bytes. Each later capture compares every source
page against the frozen snapshot, updates differing snapshot pages and records
them for restoration. Restore copies the union of those source-changed pages
and pages reported by `GetWriteWatch`, once per page. It does not rescan the
source. This preserves source changes on pages never written by rendering,
without repeating full-bank copying and comparison at the first draw.
Default runs retain full snapshot capture and full-copy restoration.

The existing pipeline drain and renderer-memory mutex exclude writers while
the page list is read, pages are restored, and tracking is reset. Tracking is
reset after the restore so those copies do not pollute the next draw's list.
This exclusion is required by the [Windows write-watch contract](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getwritewatch).
The storage retains raw guest byte order, stays host-owned and never replaces
authoritative RAM. API failure, unsupported page size or an out-of-bank page
aborts the probe. This is Windows-only experimental tooling.

The option forces strict checks. `bank-captures.json` requires one full-source
capture or comparison per trial; `bank-resets.json` requires one restore per
owned draw, a complete initial seed and no source rescans. Both record copied/
compared bytes, page size and verification. `--verify-write-watch` compares each
captured snapshot with authoritative RAM and each restored bank with that
snapshot before applying interpolated inputs. It requires `--write-watch-reset`.
These comparisons add to the `owned_capture` and reset timings, so omit
verification for performance measurements. Capture timing also includes its
diagnostic output; overlapping intervals must not be added as independent work.

```powershell
python tools/render_split_prototype/test_render_bank.py --reference-build build
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode pose-snapshot --warp '1:0:arcade:3' --settle-frames 240 --hold-accelerator --wheel-poses --shadow-inputs --write-watch-reset --verify-write-watch
```

Host checks cover same-value stores, first/last bytes, page boundaries, writes
from another thread, overlapping/disjoint source and render writes, accumulated
changes across several captures, clean-page changes and the full-copy fallback.
Native driving verification matched all 8 MiB in six captures and 18 restores,
repeated midpoint geometry exactly and matched every current endpoint, with
wheel/shadow interpolation and renderer readback enabled.
A 120-group driving endpoint profile retained all 480 ordered swaps and passed
correctness checks, but failed timing at 76.0 swaps/s. Capture averaged 2.420 ms
per group and restore work 0.235 ms per owned draw. Its full-copy control queued
all trial callbacks but stopped producing swaps during startup, before the
first prototype capture, then failed the final presentation audit. That control
cannot establish comparative throughput. Sustained high-refresh qualification
and a valid performance control remain outstanding; experiment history is in
[draft PR #61](https://github.com/alondero/worlddriverchampionship-recomp/pull/61).

## Full-sync profiling and owned readback experiment

```powershell
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode motion120 --warp '1:0:arcade:3' --settle-frames 180 --profile-rt64
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode motion120 --warp '1:0:arcade:3' --settle-frames 180 --profile-rt64 --no-owned-readback
```

`--profile-rt64` compiles a copied `rt64_state.cpp` ahead of the reference RT64
archive. Guarded substitutions time full-sync and its framebuffer/upload waits.
Six sequential `rt64_stage_*` observations partition preparation, tile/call
setup, upload setup, framebuffer processing, materials/cache work and final
workload queuing. Every complete extra task must report all six stages. The
stages partition full-sync; nested wait timings are included in those stages
and must not be added to them. Additional nested timings isolate framebuffer
pair tile processing, pipeline creation waits, command recording and submission.
Within pair processing, TMEM load and upload/hash/cache lambda timings accumulate
locally and emit once per pair; per-operation clock calls still add overhead.
`rt64-profile-source.json` records current-source/copy hashes and the compiler
command; current reference-source edits affect this optional path. Compiler
output stays in local `rt64-profile-build.log`. The launcher requires a full-sync
observation for every complete extra-frame task. Nested timings overlap;
they are instrumented wall times, not CPU utilization measurements. Nested
measurements accumulate on the graphics thread and are written after BOSS
returns, keeping file output outside the full-sync interval. Clock calls and
record collection still add measurement overhead.

RT64 defaults to rendering framebuffer data back into guest RAM.
`--no-owned-readback` temporarily sets its per-state readback override to zero
for owned-bank tasks, restoring the prior override after all task processing.
Normal tasks retain the original readback policy. The flag requires an
owned-RAM mode; `readbacks.json` checks the policy against every submitted task's
memory identity. Reference sources and production settings remain unchanged.

Owned-bank output is discarded after its presentation fence, so this is a
candidate for avoiding work on intermediate frames. It remains opt-in: geometry,
original-state and presentation-order checks do not establish pixel equivalence
or correct behavior for every framebuffer feedback effect. This option does not
remove the existing task/presentation ownership fences. Profiling results and
performance failures are recorded in draft PR #61.

## Ownership recovered

ROM reads with `tools/rom/inspect_code.py` and unchanged generated bodies
establish these boundaries (hex addresses/offsets):

| State | Producer and lifetime |
| --- | --- |
| Car `+B80/+B88`, associated `+B84/+B8C` | `28314` waits at `28344` for SP_BREAK, then `283C8..28428` finds signed lane-0 minima/maxima over four 16-byte transformed records and stores their associated signed `+E` lanes; `28448..2845C` may subtract 8 from the associated values. Derived view bounds, not car motion. |
| Car `+B90/+B94`, `+B98/+B9C` | Two geometry-list head/tail pairs. `284CC` initializes the first head to `10000000`; `28C04` saves its final tail. `28F10..28FA0` builds the second pair. `29D6C..29DD8` splices both into sort buckets. Addresses depend on the current arena; never interpolate or restore them independently of their lists. |
| Per-view record `800ABE40`, stride `1418` | `10A5C` clears its list via `F8BC` (`+68` head, `+6C` append link), builds the camera and transform requests, saves matrix `+1C..+5B`, normalization `+5C` and transform base `+18`. `10ECC` restores these values and walks the newly built list. |
| Main car-body pose | `218F0` at `21F78` passes `car+14` to `1A97C`, copying 16 float-matrix words into `800B2B00`; `21FFC` subtracts camera position from its translation before conversion. Body translation is `car+44..4C`; shadow/wheel paths are separate. |
| Wheel poses | `218F0` at `22254` copies a 64-byte matrix from `car+94`; the four-wheel loop advances the source by `84` at `22370`, giving `+94/+118/+19C/+220`. `17164` generates these matrices, starting at `171F0` and advancing by `84` at `1734C`. Camera-relative conversion follows the copy. |
| Shadow cache/input ownership | `11BA0` clears `car+9F8/+9FC` each draw. `3062C` rebuilds `car+5E4` when bit 0 of `+9F8` is clear. With `8007DF40 == 0`, it uses `car+360` and body basis `+14`, then copies `+36C` into translation `+614`. The alternate path derives a normal/height from `+C6C/+C78/+C84/+C90`; interpolating the cached matrix before drawing would be overwritten. |
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

The `pose-snapshot` mode interpolates camera and main car-body rigid poses in
owned RAM. It is a midpoint-repeat/endpoint validation sequence, not a
chronological presentation scheduler. Wheels, shadows and effects are not yet
interpolated. The owned-RAM modes use private render RAM; other modes still borrow authoritative RDRAM (the camera
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
The combined mode also waits for all requested presentation callbacks before
reusing a bank: RT64 reads framebuffer RAM during `update_screen`, which is
queued after task completion. Mutual exclusion alone would permit reset before
that queued read.
`osSendMesg` and `osRecvMesg` use the live queue and thread structures. With
runtime patch 0021 the caller supplies the result address separately. An owned
receive writes the message word into the snapshot before the runtime resumes
waiting senders. Normal receives use the same dequeue path with a destination
in live RDRAM. A nonblocking empty queue preserves the destination, and a
null destination consumes one message. The runner checks all three cases on
the guest thread and records the queue implementation in `shadow-source.json`.
Older reference builds use the copied queue object and thread-local adapter.

Next: classify remaining authoritative changed ranges, qualify the snapshot
handoff across transitions and reduce the combined path's measured overhead.
The bounded owned-RAM geometry proof and projection fixture do not establish
full gameplay/audio equivalence or snapshot behavior in every scene. The
`shadow-snapshot` drain/bootstrap experiment retains stock VI waits;
`shadow120` combines it with independent presentation and services live runtime
queues while pacing owned frames. It preserves exact frozen geometry and
observed camera/car/context state, including a controlled owned-camera offset.
Logging overhead materially distorts timings: complete diagnostic lines are
written once without dropping matrix or changed-word evidence. A short practice
timing pass does not establish sustained 120 Hz; longer practice and eight-car
workloads still fail that gate. Use longer `--trials` runs before interpreting
timing results. The combined path is not an accepted high-rate scheduler.
Run measurements and profiling evidence are recorded in
[draft PR #61](https://github.com/alondero/worlddriverchampionship-recomp/pull/61).
Interpolation, rotations, discontinuities and effect handling still
need explicit policies before this can become a shipping option.
The direct-presentation experiment is bounded to progressive single-view racing;
it skips only swap routine `013DC` receives on VI queue `800AAA28`, retaining
SP_BREAK and DP completion waits. Its copied runtime locks VI state transitions
and queues owned VI register values on the existing serialized graphics queue.
Pacing services external messages, allowing input/audio workers to run. It
still needs non-integer ratios, transition handling and sustained validation.
Do not promote these wrappers into a shipping
option; recover explicit interfaces after the experiment establishes ownership.

### Palette-index hash experiment

`--fast-texture-hash` compiles a copied `rt64_rdp_tmem.cpp` and copied hasher
header. It stops collecting palette indices once all 16 (CI4) or 256 (CI8)
indices have been seen; texture bytes and the selected palette bytes still go
through the original hash algorithm. Upload residency/eviction remains intact.
CI8 saturation is checked in blocks of 64 iterations. No cross-frame texture
cache is introduced. This option forces the existing strict geometry/state gates.
For partially populated palette bitsets, contiguous used entries are now hashed
in one update rather than separate eight-byte updates. The byte sequence and
final hash stay identical. Full 64-bit sets retain the existing fast path; the
remaining run lengths are 1..63, so the bit scans and shifts stay defined.

`--cache-palette-indices` requires `--fast-texture-hash` and adds an optional
per-thread cache of palette-index membership for contiguous 64..2048-byte
texture chunks. Its 256 entries store exact bytes, length, CI4/other indexing
mode and a 256-bit membership set (about 0.5 MiB per renderer thread). A fast
fingerprint selects a slot; reuse also requires a byte-for-byte match. Each
chunk's membership is computed independently before merging with prior rows.
Palette values and the final texture hash are always computed from current
data. Residency, GPU upload and eviction decisions remain RT64's responsibility.
The cache retains no TMEM pointers and is isolated to the copied hasher.

`texture-hash.json` includes lookup/hit counts; a cache experiment must exercise
lookups and retain all hash, geometry and ownership checks. The host fixture
compares cached results after palette changes and forces fingerprint collisions
with changing texture bytes. Performance must be measured without differential
verification; cache hits alone do not establish a throughput improvement.

`--batch-texture-hash` requires `--fast-texture-hash` and concatenates the exact
texture, palette and metadata byte stream into an 8 KiB stack buffer before
one XXH3 call. If the stream exceeds that buffer, it initializes the original
streaming hasher, feeds the buffered prefix once, and streams the remainder.
It does not change the seed, secret, byte order or which bytes participate.
The buffer belongs to one hash invocation, allocates no heap memory and retains
no input pointers. It can be combined with the independent palette-index cache.
The host fixture compares both combinations with the original and checks empty,
capacity-edge, oversized-first-chunk and multi-row fallback cases. Native
`--verify-texture-hash` compares every result with the original implementation;
timing comparisons must omit that differential verification.

`--verify-texture-hash` additionally recomputes every result with the original
hasher and aborts on a mismatch. `texture-hash.json` records task-level call and
comparison counts; the runner requires evidence for every complete trial task.
Verification changes timing and should be omitted from performance comparisons.
Its environment setting is read once per process, outside repeated hash work.
`fast-hash-source.json` records source/header hashes and the compiler command.
Reference files and production settings remain unchanged.

```powershell
python tools/render_split_prototype/test_fast_hash.py --reference-build build
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode pose-snapshot --warp '1:0:arcade:3' --settle-frames 180 --fast-texture-hash --verify-texture-hash
```

The host comparison uses 20,000 synthetic TMEM cases against the reference
header, including sparse and dense palettes, varying formats, stride/wrap cases
and hash versions, plus all 32,896 contiguous CI8 palette ranges, including
bitset boundaries and full sets. A 30-group driving endpoint run matched all
186,609 native hashes against the original and passed retained correctness
checks. Native verification covers the actual race inputs. Neither
comparison establishes general renderer pixel equivalence or production readiness.

Separate 120-group driving profiles with verification disabled retained all
480 ordered swaps each. The optimized run measured 1.160 ms in tile processing
and 2.400 ms in full sync, versus 1.314 and 2.488 ms in the original-hasher
control. Mean owned triangle counts were comparable (1239.7 versus 1240.2).
Total owned task time was effectively unchanged (6.014 versus 6.004 ms), and
swap rates were 97.6 versus 98.3/s; both failed the 120 Hz timing gate. Thus the
bounded profile shows lower tile-processing cost, not an end-to-end performance
gain. The optimization stays opt-in. Both runs used held accelerator, wheel/
shadow interpolation, explicit audited presentation and RT64 profiling, with
owned readback and full-RAM scans disabled.

### Non-integer presentation cadence

`shadow-rate --present-hz 144` extends the frozen `shadow120` experiment with
rational group counts and nanosecond pacing. For a nominal 30-group/s cadence,
144 Hz produces 4/5/5/5/5 presentations over five groups; each group includes
one normal draw plus owned repeats. The period accumulator avoids truncation
drift at rates whose periods are fractional nanoseconds. VI/AI remain 60 Hz.

This is a presentation/ownership experiment, not interpolated 144 Hz motion.
Group scheduling is nominal and does not yet adapt to consumed-sample gaps or
missed deadlines. Wall-clock throughput, motion timing under load, and physical
scanout require separate validation. The existing lag catch-up policy remains.
Strict geometry/state gates are mandatory, with variable task counts, cumulative
audio gating and ordered presentation labels checked independently by the runner.
Modes other than `shadow-rate`, `motion-rate` and `motion-samples` retain their
120 Hz setting.
Targets accept integers, decimals and exact ratios such as `120000/1001`.
The runner records the reduced ratio in `setup.json`. A decimal such as
`119.88` requests that exact decimal. Motion modes accept rates above
30 Hz through 240 Hz. The frozen control starts at 60 Hz so every
30 Hz group contains a normal draw and at least one owned repeat.

```powershell
python tools/render_split_prototype/test_cadence.py --reference-build build
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode shadow-rate --present-hz 144 --trials 30
```

The host test verifies exact presentation totals and accumulated period lengths
for one simulated hour at 60, 75, 90, 120, 144, 165 and 240 Hz. It does not wait
an hour or establish native performance. The same test runs under CTest as
`wdc_render_cadence`. It also checks gap-free slots, interpolated positions
and exact nanosecond totals over a full denominator interval at 60000/1001,
120000/1001 and decimal 59.94 Hz.

`motion-rate --present-hz 144` adds camera/body interpolation to the rational
cadence. Each group draws all scheduled poses in owned RAM and suppresses the
normal presentation; the original draw still runs for normal audio. Unlike
restarting evenly spaced blends inside each group, alpha follows the continuous
presentation grid. At 144 Hz the first group ends at alpha 5/6 and the next
starts at 1/24; every fifth group includes an exact current endpoint. Endpoint
geometry is checked whenever that endpoint lies on the grid. Pose-snapshot
remains the independent repeated-midpoint/current-endpoint validation.

The runner checks each alpha, ordered owned callbacks, variable task counts and
unchanged original state. It also reports consumed-sample gaps: this schedule
still assumes nominal 30 Hz groups, so gaps other than one need adaptive timing
before uniform motion can be claimed under load. Wheel/shadow/effect inputs and
full discontinuity handling remain incomplete. Use the same target range and
optional diagnostic/performance flags as `shadow-rate`.

### Sample-based motion scheduling

`motion-samples --present-hz 144` derives its interpolation span from the actual
consumed-sample difference between saved poses. It selects grid times inside
that span, capped at `ceil(target / 30)` owned draws per group. When more slots
are available, it skips the oldest ones and uses the latest slots. For example,
a two-sample interval from 1 to 3 at 144 Hz keeps slots 10..14: alphas 13/24
through 23/24. This bounds rendering backlog while preserving source-time
positions, rather than stretching a nominal group across missed samples.

`schedule.json` records each span, selected slot/count and skipped count. The
runner reconstructs them from observed simulation counters, checks the budget,
and checks every pose alpha and reset flag. Existing geometry, ownership,
audio-gating and callback-order gates remain mandatory. Counter subtraction is
unsigned; the existing zero/>4-sample and pose-identity/distance reset policy
starts a fresh local timeline with equal previous/current poses.

The default handles gaps already observed between snapshots; the optional
selector below also handles elapsed slots within a group. Neither establishes
end-to-end display latency or completes wheel/shadow/effect interpolation.
A reset uses a synthetic one-sample span of identical poses and reanchors the
presentation grid. The VI/AI clock and authoritative simulation stay unchanged.

`motion-samples --skip-late-draws` additionally checks a monotonic host timer
before starting each owned draw. Relative to the beginning of that group's
render loop, it advances to the latest due slot without repeating earlier
slots, clamps to the last selected slot, and renders that final pose once.
Slots skipped between snapshots and slots skipped within a group are recorded
separately. No in-flight task is cancelled and all bank/presentation fences
remain in place.

`selections.json` records elapsed nanoseconds, contiguous draw labels, selected
grid indices/slots and skip counts. The runner reconstructs every decision,
checks that the final slot was rendered, and uses the selected slot for alpha,
geometry/task-count, audio-gating and callback-order validation. Existing modes
and the default sample scheduler retain their previous behavior.

This is group-relative deadline selection, not an end-to-end display deadline
or latency guarantee. The existing swap pacing/catch-up policy remains; a
costly draw can still finish late. Visual and audio qualification are still
required even if the bounded timing gate passes.

### Wheel pose experiment

`--wheel-poses` includes four wheel matrices per active car alongside the camera
and body poses. The renderer copies these matrices independently; leaving them
at the current simulation pose while blending the body can separate their motion.
The same finite rigid-matrix validation, shortest-arc rotation interpolation,
linear translation, reset policy and exact endpoints apply. Only the owned bank
receives blended values. Original car words remain covered by the state gate.

`--wheel-poses only` is a `pose-snapshot` control: camera/body inputs stay exactly
current while wheel poses blend. The midpoint must repeat, the endpoint must
match normal geometry, and wheel-only changes must affect ordered triangle
headers. Runtime observations in `wheel-reads.json` establish that the original
matrix-copy function reads wheel addresses from the owned bank; invisible/LOD
wheels need not be copied, so this is not a per-wheel visibility assertion.

```powershell
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode pose-snapshot --warp '1:0:arcade:3' --settle-frames 180 --wheel-poses only
```

`poses.json` separately counts changed camera, body and wheel poses. The capture
must contain one camera and five matrices per car. This matrix interpolation
cannot recover extra wheel revolutions between samples; high-speed spin aliasing
and visual quality remain unqualified. Optional shadow ground-input interpolation is described below; effects
remain separate work. The feature is opt-in for all pose modes.

### Shadow ground-input experiment

`--shadow-inputs` captures the inputs consumed by the original shadow rebuild,
not its cached output matrix. Active cars are identified by the existing
`car+2F4` guard. With `8007DF40 != 0`, capture includes `car+36C` XY and the four
3D contact points at `+C6C/+C78/+C84/+C90`; the renderer derives normal and height.
The zero-selector path captures the unit normal at `+360` and position at
`+36C`. Positions blend linearly; normals blend and renormalize, with exact raw
endpoint bits. All vectors must be finite and normals unit length within the
bounded host tolerance. Invalid inputs abort rather than writing guesses.

Only owned RAM is changed. Path/active-set changes reset history; ground-position
jumps over 64 world units or normals at least 90 degrees apart also reset it.
The active-set identity uses `car+2F4 != 0`, not the raw reference. Producer
`17500..17508` copies the current ground-contact reference from `car+E40`;
consumer `30644..30648` only checks presence. Driving evidence showed ordinary
nonzero reference changes resetting history every frame under the former raw
identity check. Presence tracking retained history for all five post-initial
groups in a six-group driving capture, restoring intermediate geometry.
`history.json` records identity/count/gap/jump reset causes before history
replacement. Later guards are skipped once an earlier reset cause is found, so
false jump flags do not independently establish continuity.
Driving validation initially failed exact normal-versus-current-endpoint
geometry checks, including a control before the identity fix. A frozen control
reproduced the mismatch in two of six groups despite equal counts (1291 and
1306 triangles). Consecutive owned draws matched each other in all six groups.
`header-differences.json` now records differing-triangle counts/indices and the
first mismatching pair of eight-word headers when triangle counts agree.
This localized the mismatch to the final triangle: the HUD tachometer needle.

The HUD builder selects `view+2C4` at `2E3A4`, reads the float32 angle at `+14`
(`view+2D8`) at `2E440`, and smooths/stores it at `2E4C0..2E508` each draw.
Reusing post-normal RAM therefore advanced the needle twice. The existing
layout-hook boundary now captures its pre-normal input; owned post-normal
extras replay that input before the original smoothing code. Authoritative
RAM keeps its one normal update. `hud-needle.json` must contain one replay per
owned draw. The current single-view scope is explicit; `endpoint120` draws
extras before normal and does not use this replay.

A second driving fixture used track ID 2, car 0 and three arcade laps with
held acceleration. After 240 eligible draws, the normal and both owned extras
had identical ordered triangle headers in each of six groups (1294, 1291, 1306,
1307, 1305 and 1299 triangles). Consumed sample counts and the sound selector
held steady during every pair. Using the same setup with wheel and shadow
interpolation, the two midpoints matched in all six groups. Four groups had
different midpoint geometry from the normal draw. All six current endpoints
matched the normal draw's ordered headers.

```powershell
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode shadow-snapshot --require-repeatable --warp '2:0:arcade:3' --hold-accelerator --settle-frames 240
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode pose-snapshot --wheel-poses --shadow-inputs --warp '2:0:arcade:3' --hold-accelerator --settle-frames 240
```

The corrected frozen driving run matched all ordered normal/extra headers in
all six groups. A combined driving capture then passed camera/body/wheel/shadow,
state, endpoint and swap gates with 17 images, only the initial history reset,
and an inspected image showing 33 mph with a coherent dial/needle. Capture
timing remains diagnostic; this does not qualify sustained performance,
high-speed spin, other HUD layouts, or general driving visual correctness.
A separate 30-group driving run without capture serialization passed retained
checks and the broad timing band at a 75 Hz target: 77 selected frames and 77
successful ordered swaps, 68.2 swaps/s with 6.09..27.74 ms intervals. Callbacks
averaged 68.5/s over 67 VI ticks. This is not steady 75 Hz delivery. The run used
sample/deadline scheduling, wheel/shadow interpolation, held accelerator and
disabled owned readback/whole-RAM diagnostics.
A longer 120-group driving profile at a 144 Hz target retained all 498 selected
swaps and passed correctness checks, but failed timing at 77.0 swaps/s with
5.93..49.52 ms intervals. Owned BOSS tasks averaged 6.355 ms, including 2.767 ms
in RT64 full sync; owned-bank reset averaged 1.221 ms. A subsequent 30-group
profile with split reset timers measured 1.236 ms copying, 0.063 ms preparing
inputs, and less than 0.001 ms waiting for the renderer mutex. It retained all
119 swaps at 74.1/s and failed timing. These are instrumented wall times on a
shared host, not isolated throughput benchmarks. The reset measurement supports
investigating memory copying, not removing renderer completion fences.
A page-comparison restore experiment compared every 4 KiB page and copied only
unequal pages. It retained correctness and all 114 swaps, but averaged 1.823 ms
for restoration versus 1.236 and 1.333 ms in two full-copy controls. It reached
68.0 swaps/s and failed the 144 Hz timing gate. The comparison implementation
was rejected; full-bank copying remains the default. This bounded result does not establish
that every form of selective restoration would be slower.
`shadow-inputs.json` records input counts/changes and observed native rebuilds.
The original `3062C` still rebuilds cached matrices and vertices after `11BA0`
clears their flags. Invisible shadows need not invoke the rebuild.

`--shadow-inputs only` isolates ground inputs in `pose-snapshot`: rigid camera,
body and wheel poses stay current. It cannot be combined with wheel controls.
The midpoint must change geometry and repeat exactly; alpha 1 must match normal
geometry, with every ground input unchanged. `--shadow-inputs` and `--wheel-poses`
can be combined in ordinary pose or motion experiments.

```powershell
python tools/render_split_prototype/test_vectors.py --reference-build build
python tools/render_split_prototype/run.py --reference-build build --rom "World Driver Championship (USA).z64" --mode pose-snapshot --warp '1:0:arcade:3' --settle-frames 180 --shadow-inputs only
```

Native Hawaii evidence covers the contact-point path. The normal-vector path
has source mapping and host math tests, but no native qualification. Geometry
checks do not establish pixel quality, shadow behavior on all terrain, complete
cut handling, effects interpolation or audio fidelity.
