"""Average rate alone must not hide bursts or stalls in actual swap evidence."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/render_split_prototype"))
from swap_timing import summarize_swaps


def events(intervals):
    result = [{"us": 0}]
    for interval in intervals:
        result.append({"us": result[-1]["us"] + interval})
    return result


class SwapTimingTests(unittest.TestCase):
    def test_same_average_distinguishes_even_and_bursty_delivery(self):
        even = summarize_swaps(events([10000] * 20), 100)
        bursty = summarize_swaps(events([1000, 19000] * 10), 100)
        self.assertEqual(even["rate_hz"], bursty["rate_hz"])
        self.assertEqual((even["short_intervals"], even["long_intervals"]), (0, 0))
        self.assertEqual((bursty["short_intervals"], bursty["long_intervals"]), (10, 10))
        self.assertEqual((even["p95_ms"], bursty["p95_ms"]), (10, 19))

    def test_nearest_rank_includes_tail_and_uses_interval_count(self):
        result = summarize_swaps(events([10000] * 99 + [100000]), 100)
        self.assertEqual((result["p99_ms"], result["max_ms"]), (10, 100))
        self.assertEqual((result["interval_count"], result["swap_count"]), (100, 101))
        self.assertEqual(result["long_intervals"], 1)

    def test_reject_invalid_or_insufficient_evidence(self):
        for swaps, hz in [([], 120), ([{"us": 0}], 120), (events([0]), 120),
                          (events([-1]), 120), (events([1]), 0), (events([1]), float("nan"))]:
            with self.subTest(swaps=swaps, hz=hz), self.assertRaises(ValueError):
                summarize_swaps(swaps, hz)
