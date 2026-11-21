"""Cross-thread presentation timestamps must preserve their actual ordering."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/render_split_prototype"))
from present_latency import join_present_latency


class PresentLatencyTests(unittest.TestCase):
    def setUp(self):
        self.binding = dict(id=7, frame=60, draw=1, screen_us=1000, bound_us=6000)
        self.swap = dict(id=7, dequeued_us=2000, ready_us=3000, begin_us=4000, us=5000)

    def test_worker_can_swap_before_binding_observer_returns(self):
        row = join_present_latency([self.binding], [self.swap])[0]
        self.assertTrue(row["dequeued_before_bound"])
        self.assertEqual(row["screen_to_swap_ms"], 4)
        self.assertEqual(sum(row[k] for k in ("screen_to_dequeue_ms", "dequeue_to_ready_ms",
                                              "ready_to_api_ms", "swap_api_ms")), 4)

    def test_reject_missing_misbound_or_out_of_order_phases(self):
        for key, value in [("id", 8), ("dequeued_us", 999), ("ready_us", 1999),
                           ("begin_us", 2999), ("us", 3999)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                join_present_latency([self.binding], [dict(self.swap, **{key: value})])
        with self.assertRaises(ValueError):
            join_present_latency([self.binding], [])

    def test_zero_duration_phases_are_valid(self):
        swap = dict(self.swap, dequeued_us=1000, ready_us=1000, begin_us=1000, us=1000)
        self.assertEqual(join_present_latency([self.binding], [swap])[0]["screen_to_swap_ms"], 0)
