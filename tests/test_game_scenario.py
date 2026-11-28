import copy
from pathlib import Path
import unittest

from tools import run_game_scenario as runner


class GameScenarioTest(unittest.TestCase):
    def setUp(self):
        path = Path(__file__).resolve().parents[1] / "scenarios" / "practice-drive.json"
        self.scenario = runner.read_json(path)
        self.script, self.polls = runner.validate(self.scenario)
        self.native = {
            "schema": 1,
            "input_polls": self.polls,
            "graphics_tasks": 100,
            "presentations": 100,
            "races": [{"course": 1, "layout": 0, "car": 0, "mode": 8, "samples": 150, "displacement": 25}],
        }
        self.log = "[wdc:warp] loading track=1 car=0 mode=8 laps=3\n"

    def test_compiles_guest_controls(self):
        self.scenario["input"] = [{"polls": 3, "buttons": ["A", "START"], "stick": [-0.4, 0.2]}]
        script, polls = runner.validate(self.scenario)
        self.assertEqual(polls, 3)
        self.assertEqual(script.splitlines()[1], "3 36864 -0.4 0.2")

    def test_rejects_unrecognized_controls_and_unbounded_input(self):
        for fixture in ({"polls": 1, "buttons": ["X"]}, {"polls": True}, {"polls": self.scenario["max_vis"]},
                        {"polls": 1, "stick": [float("nan"), 0]}):
            with self.subTest(fixture=fixture):
                scenario = dict(self.scenario, input=[fixture])
                with self.assertRaises(ValueError):
                    runner.validate(scenario)

    def test_exit_and_replay_consumption_alone_cannot_pass(self):
        expect = self.scenario["expect"]
        self.assertEqual(runner.evaluate(expect, self.native, self.polls, self.log), [])
        self.assertTrue(runner.evaluate(expect, None, self.polls, self.log))
        self.assertTrue(runner.evaluate(expect, self.native, self.polls + 1, self.log))
        stationary = copy.deepcopy(self.native)
        stationary["races"][0]["displacement"] = 0
        self.assertIn("displacement", runner.evaluate(expect, stationary, self.polls, self.log)[0])
        self.assertTrue(runner.evaluate(expect, self.native, self.polls, ""))

    def test_visibility_peak_expectations_use_native_peak_fields(self):
        scenario = copy.deepcopy(self.scenario)
        scenario["expect"]["races"][0]["min_peak_visibility_list_count"] = 65
        scenario["expect"]["races"][0]["min_peak_visibility_filled_slots"] = 40
        runner.validate(scenario)
        expected = {"races": [{"min_peak_visibility_list_count": 65, "min_peak_visibility_filled_slots": 40}]}
        observed = dict(self.native, races=[{
            "peak_visibility_list_count": 64,
            "peak_visibility_filled_slots": 40,
        }])
        self.assertIn("peak_visibility_list_count", runner.evaluate(expected, observed, self.polls, "")[0])
        observed["races"][0]["peak_visibility_list_count"] = 65
        self.assertEqual(runner.evaluate(expected, observed, self.polls, ""), [])
        observed["races"][0]["peak_visibility_filled_slots"] = 39
        self.assertIn("peak_visibility_filled_slots", runner.evaluate(expected, observed, self.polls, "")[0])

    def test_frame_arena_gap_expectation_uses_native_minimum(self):
        scenario = copy.deepcopy(self.scenario)
        scenario["expect"]["races"][0]["min_frame_arena_gap"] = 65536
        runner.validate(scenario)
        expected = {"races": [{"min_frame_arena_gap": 65536}]}
        observed = dict(self.native, races=[{"frame_arena_gap": 65535}])
        self.assertIn("frame_arena_gap", runner.evaluate(expected, observed, self.polls, "")[0])
        observed["races"][0]["frame_arena_gap"] = 65536
        self.assertEqual(runner.evaluate(expected, observed, self.polls, ""), [])

        scenario["expect"]["races"][0]["min_secondary_frame_arena_gap"] = 32768
        runner.validate(scenario)
        expected = {"races": [{"min_secondary_frame_arena_gap": 32768}]}
        observed = dict(self.native, races=[{"secondary_frame_arena_gap": 32767}])
        self.assertIn("secondary_frame_arena_gap", runner.evaluate(expected, observed, self.polls, "")[0])
        observed["races"][0]["secondary_frame_arena_gap"] = 32768
        self.assertEqual(runner.evaluate(expected, observed, self.polls, ""), [])

    def test_race_visits_are_ordered_and_exact(self):
        practice, arcade = {"mode": 8}, {"mode": 3}
        expected = {"races": [practice, arcade]}
        for observed in ([arcade, practice], [practice], [practice, arcade, practice]):
            with self.subTest(observed=observed):
                self.assertTrue(runner.evaluate(expected, dict(self.native, races=observed), self.polls, ""))

    def test_ordered_log_requires_a_new_occurrence_after_the_menu(self):
        expected = {"log_in_order": ["90 Hz", "menu", "90 Hz"]}
        self.assertEqual(runner.evaluate(expected, self.native, self.polls, "90 Hz\nmenu\n90 Hz"), [])
        self.assertIn("Missing ordered", runner.evaluate(expected, self.native, self.polls, "90 Hz\n90 Hz\nmenu")[0])

    def test_live_draw_distance_requires_opt_in_and_observes_both_periods(self):
        scenario = copy.deepcopy(self.scenario)
        scenario["draw_distance_at"] = {"after_polls": 50, "value": 8}
        periods = [{"scale": 1}, {"scale": 8, "min_samples": 50, "min_frame_arena_gap": 16384}]
        scenario["expect"]["races"][0]["draw_distance_periods"] = periods
        script, _ = runner.validate(scenario)
        self.assertTrue(script.splitlines()[0].startswith("WDC_SCENARIO_2 "))
        self.assertTrue(script.splitlines()[0].endswith(" 50 8"))
        scenario["draw_distance_at"]["after_polls"] = self.polls
        with self.assertRaises(ValueError):
            runner.validate(scenario)
        expected = {"races": [{"draw_distance_periods": periods}]}
        race = self.native["races"][0]
        race["draw_distance_periods"] = [{"scale": 1}, {"scale": 8, "samples": 50}]
        self.assertIn("frame_arena_gap", runner.evaluate(expected, self.native, self.polls, "")[0])
        race["draw_distance_periods"][1]["frame_arena_gap"] = 32768
        self.assertEqual(runner.evaluate(expected, self.native, self.polls, ""), [])
        race["draw_distance_periods"].reverse()
        self.assertIn("period 1", runner.evaluate(expected, self.native, self.polls, "")[0])


if __name__ == "__main__":
    unittest.main()
