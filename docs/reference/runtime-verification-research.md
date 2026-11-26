# In-game verification in peer N64 recomp ports

These findings come from source review. Each peer uses a script to drive a
known in-game route, with different boundaries for input, assertions and
capture.

| Project | Mechanism | Primary sources |
| --- | --- | --- |
| Automobili Lamborghini Recompiled | Checked-in scenarios pair isolated profiles and controller recordings with native JSON results, bounded runs, and outcome assertions. Different fixtures exercise menus, warp, movement, presentation and renderer features in the ordinary executable. | [Automation harness](https://github.com/alondero/automobililamborghini-recomp/blob/b17dbd326b14a05685a4a891babf9bde82e9d47e/docs/automation-harness.md), [Scenario runner](https://github.com/alondero/automobililamborghini-recomp/blob/b17dbd326b14a05685a4a891babf9bde82e9d47e/tools/run_game_scenario.py), [Menu-to-race scenario](https://github.com/alondero/automobililamborghini-recomp/blob/b17dbd326b14a05685a4a891babf9bde82e9d47e/scenarios/menu-to-race-smoke.json) |
| AeroGauge | A race warp, held N64 buttons and stick values, a scheduled input change, and a VI budget make the shipping executable scriptable. Its Turbo test uses GDB to assert a guest flag and timer in both enabled and disabled runs. | [Input hook](https://github.com/alondero/aerogauge-recomp/blob/e0cda5263917005b476bdce13bd06632f8ac6188/src/main.cpp#L632-L651), [Turbo test](https://github.com/alondero/aerogauge-recomp/blob/e0cda5263917005b476bdce13bd06632f8ac6188/tests/test_turbo_boost_e2e.ps1#L34-L59) |
| GT64 Recompiled | A controller-1 input file drives the boot menus into Free Run. The script polls for a race mode marker, then captures the Vulkan window under Xvfb. It still captures when polling expires, so the log is needed to verify that Free Run was reached. | [Race script](https://github.com/tijayfly/gt64_recomp/blob/877b3fa8009a77774f38c1c3806787fd8c2cd053/scripts/torace.sh#L7-L19), [Input hook](https://github.com/tijayfly/gt64_recomp/blob/877b3fa8009a77774f38c1c3806787fd8c2cd053/src/main/main.cpp#L243-L274), [Window harness](https://github.com/tijayfly/gt64_recomp/blob/877b3fa8009a77774f38c1c3806787fd8c2cd053/scripts/headless.sh#L2-L20) |
| THPS64Host | A Python harness launches the game with a pinned test profile and a local stdin/stdout control channel. Ride playback starts at a game-owned course boundary, checks that the entire input stream was consumed, and can wait for a completed composited frame. The canonical recording also observes game progress such as a jump. | [Process harness](https://github.com/THPS64Recomp/THPS64Host/blob/eec0ae70f44c6b1c0eb5b5c1c87931c2139b4023/host/tests/harness.py#L84-L120), [Playback and capture](https://github.com/THPS64Recomp/THPS64Host/blob/eec0ae70f44c6b1c0eb5b5c1c87931c2139b4023/README.md#L121-L156) |

## Applying these patterns in WDC

The [runtime verification workflow](../runtime-verification.md) is the entry
point for WDC's runner and checked-in scenarios.

- Start a normal player in an isolated working directory with the verified ROM,
  known graphics settings, and a hard timeout. Use `WDC_WARP` for race-only
  cases. Drive the normal input path when the menu or controller is the feature
  under test. The existing smoke runner already checks a two-race renderer
  transition. [Player smoke](../../tools/render_split_prototype/player_smoke.py),
  [Verification levels](../testing.md)
- Wait for the intended race and a gameplay signal before collecting evidence.
  A loader marker confirms setup. Advancing simulation samples and movement
  under scripted input establish racing. The fixed-rate fixture already checks
  180 consecutive physics samples and verifies player movement in an instrumented
  build. For reusable input replay, count guest controller reads from the
  selected race boundary and verify consumption, as THPS64Host does.
  [Race warp](../../src/game_warp.cpp),
  [Physics fixture](../../tools/render_split_prototype/verify_gameplay.py),
  [THPS64Host input boundaries](https://github.com/THPS64Recomp/THPS64Host/blob/eec0ae70f44c6b1c0eb5b5c1c87931c2139b4023/README.md#L121-L142)
- Keep the command, runtime settings, race markers, logs and labelled captures
  together. Capture the spawned window by PID for visual review. Use a renderer
  completion boundary for a same-frame comparison. Use fresh boots as the
  repeatable setup, with development RAM checkpoints for features that
  specifically require them. [Windows capture](../../tools/capture-window.ps1),
  [Development checkpoints](../debugging.md#development-save-states),
  [THPS64Host capture fence](https://github.com/THPS64Recomp/THPS64Host/blob/eec0ae70f44c6b1c0eb5b5c1c87931c2139b4023/README.md#L147-L156)
