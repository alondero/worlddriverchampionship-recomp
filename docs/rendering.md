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

### 1. Simulation step (variable timestep)

The simulation advances by measured wall time, not by a fixed tick, and the
time is measured by the **input sampler**, not by the renderer:

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
- **Timestep.** `func_8002C0B8` accumulates `osGetCount` ticks into a 64-bit
  time at `0x8007FDF8`. It converts the delta since `0x8007FE00` to
  nanoseconds (`x 64 / 3`; the counter runs at 46.875 MHz), divides by 1e9
  (the float at `0x8009AC50`), and stores the result in **seconds** at
  `+0x1F0` of the snapshot slot.
- **One sample per frame.** Both frame paths, `func_8000C560` (racing) and
  `func_8004E950`, start with "while `0x8007FE14` is zero, wait one VI"
  (`func_800025D8`). `func_8002CA3C` then copies one snapshot to
  `0x800B7400`, so the delta the frame simulates is at `0x800B75F0`.
- A non-zero pair at `0x8007FDF0` substitutes a fixed delta. It is probably
  used for demos or replays (unconfirmed).
- `func_8000A5A0` copies that value into `0x8007E768`, whose initial value is
  1/60, before physics (`func_80017144`). It copies a quarter of it instead
  when record `+0xAC8` is clear, which suggests four substeps.
- That step also loads a 1/60 s constant (`0x8009A6D8`) into `f28`. Whether it
  is a substep size or a clamp is not yet known.
- Player records start at `0x800BA7F0`, stride `0x18A0`.

Measured live in Hawaii practice, the race clock tracks wall time whatever the
frame rate. HUD clock against wall-stamped screenshots:

| Frame rate | Race clock advance | Wall time |
|---|---|---|
| ~30 fps (normal) | 10.07 s | 10.02 s |
| ~17 fps (40 ms added to each graphics task) | 9.99 s | 9.98 s |

Rendering more often should therefore not change game speed. Logic that counts
frames instead of reading this delta would, so it must be audited before
running faster (see [open questions](#open-questions)).

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
   [simulation step](#1-simulation-step-variable-timestep)). A frame cannot
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
  forced to 0. Racing held 60.0 fps in every measured window. Game time is the sum
  of the deltas the frames consumed (`0x800B75F0`), compared with the
  game thread's wall clock over each window, and it tracked wall time as
  closely as the stock 30 fps run. The one exception, 0.984, was a
  59.1 fps window.

Lifting only the divider should not work. This is reasoned from the code
and was not measured: samples would carry 1/60 s deltas, but at two VIs per
frame only 30 would be consumed per second. The 8-slot ring would fill and
the worker would skip samples, so game time would fall to half of wall time.

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

Let the game itself produce a frame every VI. The variable timestep keeps game
speed tied to wall time, so a higher frame rate does not by itself change game
speed. Three things have to hold for that to be true:

- **The game's two frame limiters have to be lifted together**, so a frame
  really does happen every VI: the pre-swap VI wait at `0x8000159C`, and
  the race's input poll divider (1 at `0x80037B44`). Both are described in
  [what holds racing at 30 fps](#what-holds-racing-at-30-fps). A throwaway
  probe that lifted both held Hawaii racing at 60.0 fps with game time
  tracking wall time. The graphics task is not the blocker: at 6.4-8.0 ms
  it fits inside one VI (see
  [two measurements](#two-measurements-of-the-same-task)). The remaining
  work items and the acceptance criteria are in issue #39.
- **Logic that counts frames instead of reading the measured delta has to be
  found and changed.** Statically, the frame paths `func_8000C560` /
  `func_8004E950` and their callees; live, by comparing 30 and 60 fps for
  race clock against wall time, lap time under fixed input, menu and HUD
  animation, AI pace, demo and replay playback, and audio sync.
- **The physics step has to be sound at a 1/60 s frame step**, which means
  understanding record `+0xAC8` and the 1/60 constant above.

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

The only route left is a **guest render split**: re-run camera build, ZSort CPU
emission and BOSS from an interpolated copy of simulation state between two
ticks. That needs reverse engineering to separate simulation from rendering in
the frame paths. It also needs every piece of render-relevant state found
(player and AI records, the camera object, particles), and proof that
rendering has no side effects on simulation. It is deferred until native
60 fps works.

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

The work items for these live in issue #39; this list is what is not known
yet.

- Is it safe to skip the pre-swap VI wait outright? It was replaced by a
  posted message in the probe; a reviewed hook should keep the swap aligned
  to a VI. Removing the post-swap wait instead was not tried.
- What does the race's per-sample callback `func_8000C0D0` do, and does
  anything in it assume 30 Hz samples?
- Why did issue #38's spike record menus at 38-41 fps with the pre-swap
  wait skipped, when the later probe measured 60.0 fps?
- What did issue #38's about 17 ms spike measurement include? The bench
  built for it measures the same translated BOSS at 1.6-2.3 ms, so the
  spike's 12 ms remainder was not translated BOSS, and the spike is gone
  (see [two measurements](#two-measurements-of-the-same-task)).
- What is the 1/60 s constant at `0x8009A6D8` in `func_80017144` (substep size
  or clamp), and what does record `+0xAC8` select?
- Is the fixed-delta pair at `0x8007FDF0` used by demos or replays?
- Which logic counts frames instead of using the measured delta?
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
