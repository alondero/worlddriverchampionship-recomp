# Runtime verification

Use a built player and a verified local USA ROM to exercise the affected
route. Each scenario launches the ordinary executable with an isolated
profile, drives controller 1, and checks the game's reported outcome.

## Run a scenario

1. Build the current worktree using [Building](../BUILDING.md). For C++-only
   changes in an already generated checkout, rebuild with
   `cmake --build build --target worlddriverchampionship_modern`.
2. Run the nearest scenario or make one for the behavior under test.

   ```sh
   python -B tools/run_game_scenario.py scenarios/practice-drive.json
   python -B tools/run_game_scenario.py scenarios/menu-to-practice.json
   python -B tools/run_game_scenario.py scenarios/race-transition.json
   python -B tools/run_game_scenario.py scenarios/interpolated-warp-exit-restart.json
   python -B tools/run_game_scenario.py scenarios/track10-draw-unlimited.json
   python -B tools/run_game_scenario.py scenarios/les-gets-live-draw-distance.json
   python -B tools/run_game_scenario.py scenarios/new-zealand-arcade-unlimited.json
   ```

   Practice Drive waits at the start, holds A, releases it, and asserts that
   the Hawaii practice race simulates and the player moves. Track 10 Draw
   Unlimited exercises the bounded Unlimited draw-distance regression route.
   New Zealand Arcade Unlimited checks race loading and simulation with a
   full grid and the enlarged primary draw arena.
   Menu to Practice
   drives a normal boot, car selection, and the Quick Race menus into Hawaii
   Practice Laps. Race Transition enters a second layout in Arcade mode and
   observes the 90 Hz presentation target. Interpolated Warp Exit Restart
   exits a warped Hawaii Arcade race through the pause menu, starts another
   race from the Arcade menu, and checks that both visits present at 90 Hz.
   Les Gets Live Draw Distance applies 8× through the desktop settings path
   while driving and checks simulation progress and both packet arenas before
   and after the change. Use `--rom PATH` for a local dump
   outside this checkout and `--exe PATH` for a non-default build. Windowed
   scenarios require a graphics session.
3. Inspect the printed directory under `artifacts/scenarios/`. It contains the
   scenario, compiled input, player log, the [desktop launch record](debugging.md#last-desktop-launch) under
   `build/user-data`, native observations, and pass/fail
   report with executable and ROM SHA-256 hashes. A missing result, mismatch,
   crash, or timeout fails the run. For a behavior change, run the same route on
   the baseline and the changed build when that comparison establishes the fix.
   Report the command, the observed outcome and the evidence directory.

On Windows, add `--capture` to save `window.png` from the spawned player's
window after replay. Inspect the image for the intended state and GPU output.
The game continues with neutral controller input while Windows captures the
window. For same-frame renderer comparisons, use the
[presentation audit](../tools/render_split_prototype/README.md).

## Write a scenario

Copy the closest [checked-in scenario](../scenarios) and give it an assertion
that would fail if the intended behavior were missing.

- `start` selects the first input boundary. Use `race` for a ready race,
  `menu` for the ordinary menu dispatcher, or `boot` to include startup
  prompts. Omit `warp` to drive game menus through their normal route.
  `warp` and `warp_at` accept the [developer warp syntax](configuration.md#developer-race-warps).
- Each `input` row applies for the specified number of controller `polls`.
  `buttons` takes N64 names such as `A`, `B`, `START` and `D_DOWN`.
  `stick` takes normalized `[x, y]` in `[-1, 1]`, with positive Y up.
  Missing controls are neutral. Use a neutral row between presses and a final
  neutral row to observe the response. Polls follow the game's controller
  worker cadence, so the same route can need different timing at another
  simulation rate. `max_vis` bounds the game run. `--timeout` bounds wall time.
- `settings` seeds JSON files in the fresh profile under `build/user-data`.
  Copy only the settings needed to reproduce the feature. Use the same fixture
  for both sides of a comparison.
- `draw_distance_at` schedules one desktop settings change during a race.
  Set `after_polls` to the number of completed controller reads and `value`
  to 1–8 or 16 for Unlimited. Leave time for simulation and rendering after it.
- `expect.races` asserts the ordered list of race visits. An entry can match
  `course`, `layout`, `car`, `mode` and `laps` against native guest fields.
  The internal course and layout pair can be read from the run report. Warp
  indices refer to the separate 1-based selector and can be checked using the
  loader marker in `expect.log_contains`.
- `min_samples` asserts simulation advancement during a race visit.
  `min_displacement` asserts peak displacement from its first
  observed position, in guest world units, for a single human player.
  `min_peak_visibility_list_count` asserts the largest visible-section list
  count observed during the visit. `min_peak_visibility_filled_slots` asserts
  the largest number of entries still nonzero after the ROM's frustum culling.
  `min_frame_arena_gap` asserts the smallest observed byte gap between the
  race frame arena's ascending command cursor and descending matrix/packet
  cursor. `min_secondary_frame_arena_gap` does the same for the secondary
  packet arena. These observations are one frame behind the renderer and are
  intended for draw-distance regression scenarios.
  `draw_distance_periods` checks the exact ordered list of distances observed
  in a visit with a scripted setting change. Its `scale` is 1–8 or 64 for
  Unlimited, and each period can require its own samples and arena headroom.
  `min_menu_ticks`, `min_graphics_tasks` and `min_presentations` cover the
  whole run. Presentations count admissions to RT64. Use the presentation
  audit for timing and inspect captures for pixel correctness.
- `expect.log_in_order` checks markers in sequence. Use repeated markers to
  verify that the same setting took effect again after a transition.

For a manual route or a host settings change, launch with
`./play.ps1 -Warp '1:0:practice'` on Windows. Omit `-Warp` for a normal boot.
Use [controls](controllers.md), [development checkpoints](debugging.md#development-save-states),
and `./tools/capture-window.ps1 -ProcessId PID -Output build/window.png` to
inspect the exact window. The scripted controller operates at the SDK input
boundary, so verify host remapping, physical devices, SDL settings, or rumble
through their own paths. Keep captures and checkpoints in ignored directories.

See the [peer study](reference/runtime-verification-research.md) for the
Lamborghini, AeroGauge, GT64 and THPS64Host workflows that informed this one.
