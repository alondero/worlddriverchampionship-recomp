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

    def test_race_visits_are_ordered_and_exact(self):
        practice, arcade = {"mode": 8}, {"mode": 3}
        expected = {"races": [practice, arcade]}
        for observed in ([arcade, practice], [practice], [practice, arcade, practice]):
            with self.subTest(observed=observed):
                self.assertTrue(runner.evaluate(expected, dict(self.native, races=observed), self.polls, ""))


if __name__ == "__main__":
    unittest.main()
