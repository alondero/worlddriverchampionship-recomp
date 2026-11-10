# Rendering and frame rate

How World Driver Championship (WDC) produces a frame, what limits it to 30
frames per second (fps), and what is known about raising that. This page keeps
measured facts, the rules learned while measuring them, and the questions that
are still open. RT64 integration details (clipping, aspect, HUD fixes) live in
the [renderer reference](reference/renderer.md).

All addresses are for the USA ROM (SHA-256
`e8e3734c5d2453c3a4bc940862545e2b84bdd65299e432548276fd71bccdfd97`).
Functions are named as in the generated `RecompiledFuncs/`.

Terms used below:

- **VI**: the video interface, which raises a vertical interrupt about every
  16.7 ms (60 Hz).
- **RSP**: the N64's Reality Signal Processor. **BOSS** is the game's
  combined graphics/audio RSP microcode, which this port runs as translated
  C++ (`src/aspMain.cpp`).
- **DMEM**: the RSP's 4 KiB data memory.
- **RDP**: the rasteriser. BOSS hands it raw command ranges, and RT64 draws them.
- **SIG0**: an RSP status signal bit. The CPU sets it to tell BOSS that a
  triangle sublist is ready.
- **Display list**: the list of drawing commands the CPU builds for each frame.

## One racing frame

### 1. Simulation step (fixed per sample)

The simulation advances by a fixed step per input sample, not by the
renderer's frame time. The **input sampler** sets the step: each sample adds
`(divider + 1) / 60` seconds, so 1/60 s for menus and 1/30 s for racing.
The race loop consumes all available samples before drawing:

- **Input worker.** The thread at `0x800B7620` (entry `0x8002C500`) blocks
  on queue `0x800B7600`. The VI thread posts message 0 to it on every VI,
  and the swap routine posts message 1, while `0x8007FE10` is set. On VI
  messages it counts down a **poll divider** reloaded from `0x8007FE1C`.
  When the countdown runs out, it reads the controllers into the next slot
  of an 8-slot snapshot ring (`0x800B97E8`, stride `0x200`; read index
  `0x8007FE18`, count `0x8007FE14`), calls `func_8002C0B8` on that slot,
  and then calls the per-sample callback at `0x8007FE24`, if set. When the
  ring is full, the sample is skipped.
- **Divider.** `func_8002CB0C(callback, divider)` starts the worker and
  stores both values. Menus call it as `(0, 0)` at `0x8004E128`, so one
  sample per VI. Races call it as `(func_8000C0D0, 1)` at `0x80037B40`, so
  one sample every second VI, 30 Hz. `tests/test_startup_rom.cpp` checks
  both stores against the translated code.
- **Timestep.** `func_8002C0B8` runs once per sample and adds a delta to a
  64-bit time at `0x8007FDF8`. The delta is the fixed pair at
  `0x8007FDF0` / `0x8007FDF4` whenever that pair is non-zero. Otherwise it
  is the `osGetCount` change since the last sample, kept at `0x8007FE08`.
  `func_8002CB0C` writes the pair: `0x8007FDF0` is 0, and `0x8007FDF4` is
  `0xBEBC2` (781,250 ticks, 1/60 s) plus one more `0xBEBC2` for each divider
  count, so the step is `(divider + 1) * 0xBEBC2`. Once the worker has
  started the pair is always non-zero, and no code path found clears it, so
  stock play always takes the fixed branch. The shared tail subtracts the
  value at `0x8007FE00`, converts the difference to nanoseconds (`x 64 / 3`;
  the counter runs at 46.875 MHz), divides by 1e9 (the float at
  `0x8009AC50`), and stores the result in **seconds** at `+0x1F0` of the
  snapshot slot.
- **Samples per frame.** Both frame paths, `func_8000C560` (racing) and
  `func_8004E950`, start with "while `0x8007FE14` is zero, wait one VI"
  (`func_800025D8`). `func_8002CA3C` then copies one snapshot to
  `0x800B7400`, so the current sample's delta is at `0x800B75F0`. Racing
  loops back from `0x8000C764` while another snapshot is available, applying
  `func_8000A5A0` to each and accumulating their deltas at stack `+0x3C`.
  The menu path similarly drains snapshots at `0x8004EA88..0x8004EA90`.
- `func_8000A5A0` copies that value into `0x8007E768`, whose initial value is
  1/60, before physics (`func_80017144`). It copies a quarter of it instead
  when record `+0xAC8` is clear, which suggests four substeps.
- `func_80017144` wraps the integrator `func_800194F0`. The 1/60 constant
  (`0x8009A6D8`) is loaded by the separate wheel animation routine
  `func_80017164`, not the integrator (see the audit below).
- Player records start at `0x800BA7F0`, stride `0x18A0`.

Measured live in Hawaii practice, the race clock tracked wall time at both
frame rates below. HUD clock against wall-stamped screenshots:

| Frame rate | Race clock advance | Wall time |
|---|---|---|
| ~30 fps (normal) | 10.07 s | 10.02 s |
| ~17 fps (40 ms added to each graphics task) | 9.99 s | 9.98 s |

Simulation time follows consumed samples. The drain loop explains how a
17 fps renderer can keep up with 30 Hz simulation: it can process more than
one sample per frame. Logic that counts samples instead of their elapsed time
can still change speed when sampling rises to 60 Hz.

### 2. Camera and display lists (CPU)

- **Camera.** `func_80011D54` builds view `0x800B2AC0` x projection
  `0x800B2A80` into `0x800B2A40`. `func_8002CF30` converts that to a
  fixed-point `Mtx` in a per-frame arena that grows downwards (top pointer
  `0x8007FE70`, counter `0x8007FE78`). The arena pointer is stored at
  `0x8007ED68`.
- **Display-list builders.** Six builders load the camera matrix:
  `0x800094BC`, `0x80010BE4`, `0x80022058`, `0x80022D08`, `0x800238A8` and
  `0x800239FC`. Each emits `04 03F870` (MOVEMEM to DMEM `0x870`), then
  `04 03F830 <model>` and `0A 000830 0870 08B0` (MTXCAT).
- **Write-back and CPU reads.** `func_80022660` has BOSS write a combined
  matrix back to RDRAM (`04 83F8B0`). The CPU reads the float view-projection
  matrix at `0x80010B60`, `0x80010FDC`, `0x80022D94`, `0x80022E40` and
  `0x80029E9C`.
- **ZSort.** Racing triangles come from the CPU's ZSort stage, which writes
  triangle sublists that BOSS consumes. They are not re-projected from the
  camera matrix inside BOSS (see [above 60 Hz](#above-60-hz)).
- **SP_BREAK handshake.** Queue `0x800B2B40` (512 messages) is registered
  for `OS_EVENT_SP_BREAK` (event 11) at `0x800241A8`. `func_80023F2C`
  counts a request at `0x8007ED74`. `func_80023F88` polls the queue and,
  if it is empty, blocks until BOSS raises a break. Its six callers
  (`0x800090A0`, `0x800256A0`, `0x80025740`, `0x80025810`, `0x8002780C`,
  `0x80028344`) then run the `cache_transform` functions
  `func_80025410` / `func_8002703C`. So the CPU's display-list build waits
  on BOSS progress within the same frame. Measured in Hawaii racing: about
  14 calls per frame, about 5 of which block, for 0.2-0.4 ms in total at
  30 fps and 0.7-1.5 ms at 60 fps.

### 3. Graphics task (BOSS)

- **Submission.** The frame submitter `func_80024FCC` submits one BOSS
  graphics task per frame. The `OSTask` builder is at `0x80024F08`, and the
  microcode text is ROM `0x7D380`, size `0xF60` (`aspMain.us.toml`).
- **Execution in the port.** `src/boss_rsp.cpp` runs the task synchronously on
  the graphics worker thread. Each DP_START..DP_END command range goes straight
  to RT64's raw `processDisplayLists`.
- **Observed task data.** Captured racing tasks had their data at `0x80351360`
  or `0x8038D360` (two buffers seen), and RDP output starting at physical
  `0x3C9360`.
- **Size of a racing task.**

  | Per racing task | Amount |
  |---|---|
  | SP_STATUS reads by BOSS | 1,500-1,700 |
  | RDP command ranges | 565-695 |
  | RDP command data | 170-225 KB |
  | RDRAM written by BOSS | 60-70 KB |

- **SIG0 behaviour.** In every captured racing task, the first status read saw
  SIG0 set and every later read saw it clear. BOSS did not wait for the CPU
  mid-task in those frames.

### 4. Presentation (swap and VI)

- **VI thread.** The VI thread `func_800016E8` registers
  `osViSetEvent(..., retrace=1)` at `0x8000171C`. It forwards every VI to
  message queue `0x800AAA28`, which holds one message (`0x80001ADC`), so a
  VI nobody is waiting for is dropped rather than queued.
- **Completion relays.** Thread `0x800017E0` forwards `OS_EVENT_SP` to
  queue `0x800AA918`, which `func_800005F4` waits on before each swap.
  Thread `0x80001888` forwards `OS_EVENT_DP` to `0x800AA988`, which
  `func_80000678` waits on inside the swap routine.
- **Swap routine.** The frame submitter calls the swap routine `func_800013DC`.
  On the normal path it does, in order:
  1. `osRecvMesg(VI)` at `0x8000159C`;
  2. `osViSwapBuffer` at `0x800015A8`;
  3. `osRecvMesg(VI)` at `0x800015BC`;
  4. `osViGetCurrentFramebuffer`, then another wait if the swap has not landed.

  That is at least two VIs per frame.
- **Observed rates.** The game submits 29.5-30 graphics tasks per second;
  RT64 updates the screen 60 times per second.
- **Not landed.** In this runtime `osViSwapBuffer` takes effect at the next
  VI tick, before that VI's message is sent, so the step 4 re-wait never
  fired in the measurements below.

### What holds racing at 30 fps

Two independent limits, each worth one VI per frame. Either one alone
holds racing at exactly 30.0 fps.

1. **The swap routine waits for two VIs per frame.** Step 1 waits for a VI,
   step 2 swaps, and step 3 waits for the next VI. The next graphics task
   is only started after step 3 returns, so every frame costs at least two
   VIs, whatever the CPU and graphics task cost.
2. **Races sample input, and therefore time, at 30 Hz.** The race starts
   the input worker with divider 1 (see
   [simulation step](#1-simulation-step-fixed-per-sample)). A frame cannot
   start until a snapshot is ready, so a race frame cannot start more often
   than every second VI. Menus use divider 0.

`0x8007EDBC` is not a limiter. It is set to 3 by the frame submitter and
decremented by `func_80025350`, and its only reader (`0x8000A3C4`) gates a
call to `func_800245AC` with the framebuffer's width and height. None of the
measured waits below depends on it.

Measured with a throwaway probe: hooks timed each blocking wait on the game
thread, averaged over 120-frame windows. Hawaii practice, the same machine
as [where the frame time goes](#where-the-frame-time-goes). Recorded, not
reproducible from the tree.

| Per racing frame, ms | Stock | Pre-swap wait skipped | Both limits lifted |
|---|---|---|---|
| Frame rate (fps) | 30.0 | 30.0 | 60.0 |
| Game-thread work outside waits | 0.4 | about 17 (input wait) | 0.3-0.4 |
| SP wait (`func_800005F4`) | 4.0-7.7 | under 0.2 | 3.5-6.5 |
| SP_BREAK waits (ZSort) | 0.2-0.4 | 0.2 | 0.7-1.5 |
| Pre-swap VI wait (`0x8000159C`) | 8.5-12.1 | skipped | skipped |
| Post-swap VI wait (`0x800015BC`) | 16.4-16.9 | 15.8-16.2 | 8.7-11.9 |
| Game time / wall time | 0.995-1.000 | not measured | 0.999-1.001 |

- **Stock.** Only 4.5-8 ms of each 33.3 ms frame is work: the CPU (0.4 ms)
  and the graphics task. The rest is the two VI waits.
- **Pre-swap wait skipped**, by posting a VI message when the queue is
  empty, as in issue #38. Menu screens on the route to Hawaii rose to
  60.0 fps. Racing stayed at 30.0, and the freed VI reappeared as a wait
  for the next input snapshot (the "work" row). Issue #38's spike recorded
  menus at 38-41 fps with what is described as the same hook; why the two
  differ is not known.
- **Both limits lifted**: pre-swap wait skipped and the race's divider
  forced to 0. Racing held 60.0 fps in every measured window. Game time is
  the sum of the deltas the frames consumed (`0x800B75F0`), compared with
  the game thread's wall clock over each window, and it tracked wall time as
  closely as the stock 30 fps run. The one exception, 0.984, was a 59.1 fps
  window. With fixed steps this ratio equals frame rate divided by the
  sample rate, so it confirms the frame rate. It does not show that the game
  clock is timed from the wall clock.

Lifting only the divider leaves the two-VI rendering cap. The race loop can
consume two 1/60 s snapshots per 30 Hz frame, so halving game speed does not
follow from the divider alone. That earlier hypothesis overlooked the drain
loop; the divider-only case has not been measured.

### Native 60 fps option

Enhancements exposes `native_60fps`, saved in `graphics.json`, default false.
The implementation is `src/game_framerate.cpp`; all instruction boundaries
are reviewed `[[hooks]]` in `rom-symbols.toml`. The game thread latches the
preference at `func_8002CB0C` entry. Only the measured menu `(0,0)` and race
`(8000C0D0,1)` argument pairs opt in. Other callers retain stock arguments.
The race divider becomes 0 before the ROM computes its fixed tick pair and
starts the worker. Apply during a race waits until the next worker startup;
it cannot change just the wait or just the timestep of queued samples.

Authored intro/demo scenes `0x11..0x13` retain stock 30 Hz sampling, the
pre-swap wait and all stock sample-counted behavior, even with the option on.
The game-thread hook reads the current scene enum at `0x800923E4` before
worker startup. Race setup groups these modes at `0x80036D44..0x80036D78`
and loads scene `0x11`'s original recording at `0x80036ED0..0x80036EE4`.
Those inputs were authored at 30 Hz; reading twice as many per second changes
the cars' route even though elapsed game time remains correct. The selected
preference is preserved for the following menu or playable race.
With Native 60 fps enabled, a live Windows debugger check before worker
startup confirmed scene `0x11`, divider 1 and 1,562,500 ticks (1/30 second).

Before `0x8000159C`, the hook posts message 0 nonblocking to the one-slot VI
queue `0x800AAA28`. A full queue is left intact. The original receive consumes
that message before `osViSwapBuffer`; the post-swap wait and framebuffer check
are retained. The pinned runtime's `ultramodern/src/events.cpp` implements
`osViSwapBuffer` by updating the next VI state under its message mutex.
`vi_thread_func` calls `update_vi()` before sending the VI message. Therefore
the buffer becomes current at a VI, even when the request is made between VIs.
The synthetic VI fixture exercises both an already queued VI and a delayed
buffer landing. It is not a physical display/scanout measurement.

The generated send uses a copy of the context, preserving the ROM's registers.
Native OS queue helpers own synchronization and guest byte order. The queue
is a 24-byte `OSMesgQueue` with one u32 slot at `0x8007D740`. Startup's
callback/divider/tick words and all hook register contracts are documented
beside the implementations. Disabled hooks leave guest state and queue
operations unchanged.

The frame-count audit currently establishes these boundaries:

| Logic | ROM evidence | 60 Hz treatment |
| --- | --- | --- |
| Race/countdown time | `8000A5A0` consumes snapshot seconds; `8004CD44..8004CD60` subtracts them from countdown | Original elapsed-time path |
| Per-sample callback | `8000C0D0` checks bit `0x40000` in two controller records and sets player `+0x2E4` to 3; no forces or integration | Lifetime becomes 6 samples; decrement remains in `8000C164` |
| Physics | `8000B320..8000B418`: `+0xAC8 != 0` selects one full-delta call; zero selects four quarter-delta calls to `80017144`/`800194F0` | Original integrators receive 1/60 or 1/240 second instead of 1/30 or 1/120 |
| Integration method | `80017DEC` dispatches from `8007E76C`; automatic mode compares timestep against 0.02 (`8009A7A0`) | No fixed 30 Hz timestep in this dispatcher; all three algorithms tested with a controlled force |
| Wheel animation | `80017164` advances four wheel angles using the fixed 1/60 multiplier at `800171B8` | Halve the loaded multiplier; same angle per second |
| Camera direction smoothing | `80012354` calls the 2D blend `8001A4C4` at `800124E8` with coefficient 0.25 | Use `1-sqrt(0.75)` at that caller only, retaining the original exponential decay |
| Track animations/effects | `8000BF40`, `8000BFC8`, `8000C050` receive the sum of consumed sample seconds | Original elapsed-time arguments |
| Sample counter/HUD flash | `8000C6DC` increments `8007E530`; `8002E230..8002E254` uses its age for a delay and flashing mask | Halve the unsigned age in this reader, preserving stock hold and flash cadence |
| Replay/ghost input | `80003DA4` and `80003E74` advance stream `+0x70` once per consumed sample | Original intro/demo scenes stay at stock 30 Hz; player replay/ghost compatibility still requires validation |

Replay compatibility is a known implementation gap, not just a missing visual
check for player recordings. `func_80003C38` records action/steering bytes
without timestep metadata;
`func_80003CD4` reads them once per sample. A stock recording sampled at 60 Hz
consumes its inputs twice as quickly; a 60 Hz recording played at 30 Hz has the
reverse mismatch. A rate flag for just the most recent race would not cover
loaded recordings or the separate ghost stream at `0x8007E028`. The setting
remains experimental. The maintainer deferred compatible player replay/ghost
handling from PR #46 to
[issue #54](https://github.com/alondero/worlddriverchampionship-recomp/issues/54).
The fixed 30 Hz exception for original intro/demo scenes does not reinterpret
player recordings; the compatibility warning remains in the settings UI.

A Windows D3D12/RTX 3080 run of Hawaii practice reached the scene with the
option on, but **did not pass acceptance**. At automatic resolution in a
1280x720 window, 1,374 frame intervals took 60.238 seconds (about 22.8 fps),
and consumed-sample time advanced 53.717 seconds (ratio 0.892). The first
window included the remaining countdown. Window visibility/focus was not
controlled in this run; it must not be compared directly with the earlier
60 fps probe. Repeating at 1x reached the title/demo route, with variable
rates, but did not provide another controlled Hawaii window. These results
do not isolate a hook, renderer, scheduler or window-state cause.

Measurement used temporary linker wrappers around the worker-start and
pre-swap hooks, with five seconds of warm-up and a steady-clock interval.
The sample count was read from `0x8007E530`, multiplied by the active fixed
step, so it includes all samples drained between rendered frames. No sleeps
or timing switches were inserted into BOSS or the graphics worker. This was
a local probe, not a checked-in regression test; the final player is built
without those wrappers. Stock runtime comparison, one-minute 60 fps/time
acceptance, fixed-input lap, another track, split-screen and replay remain
**not passed/not done**. Menu rates varied too, so the historical 38-41 vs
60 fps discrepancy remains unresolved.

This is a bounded audit, not proof that all AI, HUD, camera and demo paths
are independent of sample count. The integrator test uses a controlled
decaying force over one second at both rates, with all 16 state components;
it establishes finite, comparable timestep handling, not lap equivalence or
collision stability on every track. The maintainer has deferred the per-track
fixed-input lap corpus and 30/60 Hz lap comparison from PR #46 to
[issue #53](https://github.com/alondero/worlddriverchampionship-recomp/issues/53),
using the scenario infrastructure tracked in
[issue #49](https://github.com/alondero/worlddriverchampionship-recomp/issues/49).
Lap equivalence is not established. A second track, split-screen and replay
remain explicitly unverified for
[issue #42](https://github.com/alondero/worlddriverchampionship-recomp/issues/42).

## Where the frame time goes

Measured on Windows x64, Direct3D 12, RTX 3080, at automatic resolution and
original rate, in Hawaii practice racing.

Two kinds of number appear below, and they are kept apart on purpose:

- **Reproducible**: the measurement can be repeated today from the checked-in
  tools, on a capture of the game, without changing the game.
- **Recorded**: the measurement was taken with temporary instrumentation that
  is deliberately not in the tree (see [tools and methods](#tools-and-methods)),
  so it cannot be repeated on demand. It is kept here with its method and
  machine, as a fact of record.

| Per racing task | ms | Kind |
|---|---|---|
| Translated BOSS, replayed offline (`wdc_boss_bench`, byte-identical) | 1.6-2.3 | Reproducible |
| Whole task on the live graphics worker (`bossMain` wall time) | 6.4-8.0 | Recorded |
| ...RDP sink: RT64 raw decode and the HUD walk | 4.4-5.6 | Recorded |
| ...about 1,500 SP_STATUS reads (each yields the thread) | 0.3-0.4 | Recorded |
| ...remainder, matching the offline figure | 1.6-2.1 | Recorded |
| `update_screen` (presentation) | about 0.4 | Recorded |

The offline row is the median over 500 iterations of each of three captured
racing tasks, every iteration byte-identical. The live rows are averages over
90-task windows. In those frames BOSS read SIG0 set once and clear on every
later read, so it did not wait for a CPU signal mid-task.

The whole graphics task fits inside one 16.7 ms VI with time to spare, so its
cost does not explain the 30 fps cap. The cap is two VI waits per frame,
one in the swap routine and one for the race's 30 Hz input sample (see
[what holds racing at 30 fps](#what-holds-racing-at-30-fps)).

Within translated BOSS, a sampling profile puts about a quarter of the time
in the runtime's byte-at-a-time DMA helpers (`dma_rdram_to_dmem`,
`dma_dmem_to_rdram`). `L_1FAC` is BOSS's DMA routine. Most of the rest is in
vector-unit load/store helpers (`SQV`, `LQV`, `SDV`), `VADD`/`VMADN`, and the
transform loops around IMEM `0x1900-0x1CBF`. There is no single hotspot.
Word-wise DMA copies would be equivalent for this aligned, word-swapped
memory: a generic runtime improvement, not needed for 60 fps. This profile is
reproducible: `wdc_boss_bench --profile` plus `tools/boss/profile_report.py`
attributes the samples.

### Two measurements of the same task

[Issue #38](https://github.com/alondero/worlddriverchampionship-recomp/issues/38)
recorded a racing graphics task at **about 17 ms**, of which about 12 ms was
attributed to translated BOSS. The table above records the same quantity at
**6.4-8.0 ms**, of which 1.6-2.1 ms is translated BOSS. The two disagree,
and the difference is entirely in the part left over after the known costs
were subtracted:

| Part of a racing task | Issue #38 (throwaway spike) | Later live measurement |
|---|---|---|
| RT64 raw RDP decode in the sink | about 4.3 ms | 4.4-5.6 ms |
| SP_STATUS reads and waits | about 0.5 ms | 0.3-0.4 ms |
| Everything else, called translated BOSS | about 12 ms | 1.6-2.1 ms |
| Total | about 17 ms | 6.4-8.0 ms |

The later figure is the one to use. Its remainder agrees with the offline
bench, which measures the same translated code on its own at 1.6-2.3 ms.
The earlier figure's remainder was a subtraction, so it took in whatever
else the graphics worker did inside the measured window, and the bench
shows there was not 12 ms of translated BOSS in there. What that earlier
measurement did include is not established: the spike it came from was
thrown away, so it cannot be repeated.

Two consequences for the plan in issue #38:

- Its premise, that the graphics task takes longer than one 16.7 ms VI, does
  not hold on the later measurement.
- Its target for the profiling work, bringing translated BOSS from about
  12 ms down to about 8 ms, is already met by the number the bench reports.
  The live task's cost sits mostly in the RT64 RDP decode (4.4-5.6 ms of
  6.4-8.0 ms), and that decode is inside the task, so it is not what holds
  the game at 30 fps either.

## Why higher presentation rates add no motion

`src/renderer.cpp` gives RT64 raw RDP commands
(`processDisplayLists(..., false)`). RT64 creates in-between frames by
re-rendering a workload with blend weights between the previous and current
frames' transform groups (`threadRenderFrame` in
`lib/rt64/src/hle/rt64_workload_queue.cpp`). Raw `Projection::Type::Triangle`
draws carry no transform groups, so a presentation target above the game's
own rate only repeats frames. The player-facing note is in
[configuration](configuration.md).

## Routes past 30 fps

### Native 60 fps (current route)

Let the game itself produce a frame every VI. Simulation steps are tied to
input samples, and the race loop drains them before drawing. Three things
have to hold for the higher sampling rate to retain stock speed:

- **The game's two frame limiters have to be lifted together**, so a frame
  really does happen every VI: the pre-swap VI wait at `0x8000159C`, and
  the race's input poll divider (1 at `0x80037B44`). Both are described in
  [what holds racing at 30 fps](#what-holds-racing-at-30-fps). A throwaway
  probe that lifted both held Hawaii racing at 60.0 fps with game time
  tracking wall time. The graphics task is not the blocker: at 6.4-8.0 ms
  it fits inside one VI (see
  [two measurements](#two-measurements-of-the-same-task)). The remaining
  work items and the acceptance criteria are in issue #42.
- **Logic that advances once per frame, instead of by the sample step, has to
  be found and changed.** Statically, the frame paths `func_8000C560` /
  `func_8004E950` and their callees; live, by comparing 30 and 60 fps for
  race clock against wall time, lap time under fixed input, menu and HUD
  animation, AI pace, demo and replay playback, and audio sync.
- **The physics step has to be sound at a 1/60 s sample step.** The
  [audit](#native-60-fps-option) establishes the substep selection and tests
  numerical integration; fixed-input laps and collision behavior still need
  comparisons in the running game.

Any change that lifts the limiter belongs behind an option, as reviewed
`[[hooks]]` entries in `rom-symbols.toml`.

### Above 60 Hz

Not reachable by replaying BOSS:

- A finished task can be replayed byte-exactly. Replaying it with a camera
  matrix several seconds old still produced **byte-identical racing output**,
  in 2,038 of 2,038 racing replays.
- Racing makes no direct loads of the combined matrix at DMEM `0x8B0`, so
  correcting those loads changes nothing.
- The triangles come from CPU ZSort data written after the camera transform.

For geometry-based interpolation with fixed-rate gameplay, the route is a
**guest render split**: re-run camera build, ZSort CPU emission and BOSS from
an interpolated view of two simulation snapshots. Raising simulation/VI rate
is a different design: it changes integration and sample-counted behavior,
and does not meet the requirement that display rate leave gameplay unchanged.
Image-based frame generation would be another approach, but is not implemented
or evaluated here.

The existing Presentation rate setting selects RT64's output target only.
It does not implement this split. Neither changing that target nor generalizing
the four 60 Hz corrections unlocks intermediate geometry.

#### Verified split boundaries and blockers

The race loop `func_8000C560` consumes input at `8000C5B8` and `8000C75C`,
advances physics at `8000C6E8`, and updates elapsed-time effects at
`8000C788..8000C798`. Its drawing call is `8000C870 -> func_8000A30C`.
This is a useful extraction boundary, **not a repeatable pure draw function**:

| Boundary | Existing work | Requirement for intermediate frames |
| --- | --- | --- |
| `8000C648 -> 8000FC18` | View/layout and camera setup inside the sample loop | Separate camera evolution from building an interpolated view; rerunning the whole helper is not safe |
| `8000A324 -> 80011BB8` | Clears car-record words `+9F8/+9FC` through `80011BA0` | Identify the fields' readers and ownership before treating them as render scratch |
| `8000A32C -> 8001F9CC` | Rolls sound-event lists, clears current counters and toggles the list buffer | Retain event publication on the authoritative path; do not consume it on extra draws |
| `8000A378/8000A438` | Calls `80010A5C/80010ECC` for each view (`800ABE40`, stride `1418`) | Rebuild transforms and CPU ZSort from a render snapshot for each view |
| `8000A380/8000A440` | Calls HUD helpers `8004D020/8004D120` per view | Audit persistent HUD writes, including `8004D120`'s writes to `80092D2C` |
| `8000A520 -> 80024FCC` | Includes audio generation (`80043274/80042FA8`), task submission and the swap wait | Separate audio generation and authoritative buffer/queue transitions from presentation-only submissions |
| `8000A528 -> 80025350` | Decrements `8007EDBC` and clears `8007ED78` | Classify as submission/clear policy; it is not simulation timing |
| `8000A530 -> 800214C8` | Processes sound events after submission | Keep event processing at the authoritative cadence |

`wdc_render_boundary_rom` executes the translated draw root, car-field reset,
sound-list rollover and submission cleanup. Drawing, task submission and the
sound consumer are observed doubles. With zero, one and two views it checks
call order, view addresses and saved registers. Calling it twice without
advancing simulation really does toggle the sound-list buffer twice and replace
the previous event list with an empty one. This is an executable counterexample
to treating `8000A30C` as a pure draw function. It is not a complete write audit,
an audio synthesis test, or proof that repeated rendering is safe.

The pinned runtime's `ultramodern/src/events.cpp::vi_thread_func` also couples
the 60 Hz clock to screen updates, framebuffer landing, VI messages and AI
messages. Presentation-only frames need their own scheduling and buffer
ownership; changing this loop's rate would also change guest event delivery.
The existing renderer's `get_display_framerate()` already reads RT64's display
rate, but that does not provide an independent game render clock.

#### Implementation sequence

1. Recover the draw root and separate sound-event publication/consumption,
   audio generation and authoritative frame bookkeeping from repeatable
   geometry construction. Preserve the original ordering at 30/60 Hz against
   the translated reference. Audit transitive writes, including HUD and
   per-view helpers; a top-level separation alone is insufficient.
2. Define render snapshots for player/AI transforms, camera, wheels and effects.
   Keep interpolation out of authoritative RDRAM. Handle rotations, object
   creation/destruction, camera cuts, scene changes and save-state loads by
   resetting history where appropriate. Recompute culling and ZSort from the
   same interpolated view that draws the frame.
3. Schedule presentation independently of guest VI, input and audio clocks.
   Rebuild CPU geometry and BOSS for intermediate frames, with explicit arena,
   framebuffer and completion ownership. Preserve the CPU/RSP write-back
   handshake; do not replay a finished task with a different camera matrix.
   Bound catch-up work when rendering cannot keep up.
4. Connect the existing Display/Custom target to this scheduler. Support
   non-integer ratios (for example 60 Hz simulation to 144 Hz output), display
   changes and targets below the simulation rate. Do not add a second competing
   FPS setting before there is a working independent render path.

Acceptance requires identical authoritative state for the same input samples
at several presentation rates, plus unchanged replay consumption, timers and
audio event counts. Test 60, 75, 120, 144, 165 and 240 Hz, stalls, pause/resume,
scene transitions and split-screen. Visual checks must additionally establish
that intermediate frames contain new motion without geometry/HUD artifacts.
Interpolation between completed ticks normally adds one simulation tick of
visual latency (16.7 ms at 60 Hz or 33.3 ms at 30 Hz); extrapolation has different
prediction and correction trade-offs. Fixed-rate rendering does not by itself
fix the pre-existing incompatibility between 30 Hz and 60 Hz recordings.

Arbitrary-rate motion is **not implemented**. The boundary fixture establishes
a prerequisite for the refactor, not a new player-facing capability.

A [throwaway native prototype](../tools/render_split_prototype/README.md)
now exercises extra draws in the real player, gates the two sound-list calls,
and compares emitted triangle geometry. Its one-VI experiment retains 30 Hz
sampling, but consecutive extra draws still differ without simulation
advancing. This narrows the next investigation to render state and geometry
construction; it does not yet establish a repeatable interpolated frame.

## Tools and methods

- **BOSS task capture and replay.** `WDC_BOSS_CAPTURE` plus F10, then
  `wdc_boss_bench`, gives offline per-task timing and a byte-exact regression
  check. `--profile` with `tools/boss/profile_report.py` attributes time to RSP
  labels. See [BOSS task capture](../tools/boss/README.md).
- **Replaying BOSS needs all three of these**, or the output diverges:
  1. a journal of BOSS's RDRAM writes, so the input view can be rebuilt;
  2. the recorded SP_STATUS reads, played back in order;
  3. DP-complete held until the RDRAM view is saved.

  Forcing SIG0 on instead of replaying the reads loops the dispatcher at
  `L_1108`.
- **RDP command capture.** `WDC_RDP_CAPTURE` plus F10 records one task's raw
  RDP commands for `tools/rdp/`. See [configuration](configuration.md).
- **Game time against wall time.** Compare the HUD race clock across
  screenshots stamped with wall-clock time. The route to Hawaii practice is in
  [first playable](first-playable.md#controls-and-reproduction).
- **Live task timing.** Time `bossMain`, the RDP sink and the status reads
  separately, and compare the thread's CPU cycles with wall time to tell
  "slow" from "descheduled". Keep such timing out of the tree; periodic
  logging on the graphics worker is not allowed (see
  [configuration](configuration.md)). Figures obtained this way are the
  *recorded* ones above, not reproducible from the tree.
- **Timing pitfall.** On Windows, `sleep_for(1 ms)` is not a no-op: timer
  granularity inflates frame time. Never use a short sleep as a logging
  switch.

## Open questions

The work items for these live in issue #42; this list is what is not known
yet.

- Why did issue #38's spike record menus at 38-41 fps with the pre-swap
  wait skipped, when the later probe measured 60.0 fps?
- What did issue #38's about 17 ms spike measurement include? The bench
  built for it measures the same translated BOSS at 1.6-2.3 ms, so the
  spike's 12 ms remainder was not translated BOSS, and the spike is gone
  (see [two measurements](#two-measurements-of-the-same-task)).
- How do skipped samples (ring full) affect game time? The drain loop can
  keep up at 17 fps, so that measurement does not show a full ring. The
  worker skips `func_8002C0B8` when full; long stalls still need measuring.
- Which logic advances once per frame instead of by the sample step?
- Do the CPU's ZSort sublists copy screen-space vertices, or read transformed
  vertices back? The [SP_BREAK handshake](#2-camera-and-display-lists-cpu)
  suggests the CPU reads BOSS output back before sorting, but what
  `func_80025410` / `func_8002703C` read is not confirmed. This only matters
  for the above-60 Hz route.
- Do menus, split-screen and other tracks behave like Hawaii racing (task
  cost, SIG0 timeline)?

## Keeping this page current

- Record facts with their evidence: an address, a ROM read, or a measurement
  with its method and machine.
- Label anything unproven as a hypothesis, and move it out of
  [open questions](#open-questions) once it is answered.
- Leave session history in issues and pull requests.
- When a fact here changes, update or remove it rather than appending a
  correction.
