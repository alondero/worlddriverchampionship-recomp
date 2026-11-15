"""Negative controls for callback-to-swap evidence, without a GPU or ROM."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/render_split_prototype"))
from present_audit import validate_admissions, validate_swaps


class PresentAuditTests(unittest.TestCase):
    def setUp(self):
        self.callbacks = [{"frame": 60, "draw": i} for i in range(1, 4)]
        self.bindings = [dict(p, id=i+100, previous=i+99) for i, p in enumerate(self.callbacks)]
        self.swaps = [{"id": i+100, "index": 0, "valid": True, "us": 1000+i*7000}
                      for i in range(3)]

    def test_ordered_swaps_allow_unrelated_startup_records(self):
        startup = dict(self.swaps[0], id=99)
        self.assertEqual(validate_swaps(self.bindings, [startup]+self.swaps, self.callbacks), self.swaps)

    def test_reject_missing_duplicate_reordered_or_failed_swaps(self):
        cases = [self.swaps[:-1], self.swaps+self.swaps[-1:], self.swaps[::-1]]
        for field, value in [("valid", False), ("index", 1), ("us", 1000)]:
            changed = copy.deepcopy(self.swaps)
            changed[-1][field] = value
            cases.append(changed)
        for swaps in cases:
            with self.subTest(swaps=swaps), self.assertRaises(ValueError):
                validate_swaps(self.bindings, swaps, self.callbacks)

    def test_reject_bad_binding_and_id_reuse(self):
        for field, value in [("draw", 99), ("id", 100), ("previous", 102)]:
            bindings = copy.deepcopy(self.bindings)
            bindings[-1][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_swaps(bindings, self.swaps, self.callbacks)
        with self.assertRaises(ValueError):
            validate_swaps([], [], [])

    def test_first_callback_cannot_claim_startup_swap(self):
        bindings = copy.deepcopy(self.bindings)
        bindings[0]["previous"] = bindings[0]["id"]
        with self.assertRaises(ValueError):
            validate_swaps(bindings, self.swaps, self.callbacks)

    def test_admission_requires_every_previous_event_dequeued(self):
        admissions = [dict(previous=p, dequeued=p, wait_us=0) for p in (0, 100, 101)]
        validate_admissions(admissions, self.bindings)
        cases = [admissions[:-1], admissions[::-1]]
        for field, value in [("dequeued", 100), ("previous", 99), ("wait_us", -1)]:
            changed = copy.deepcopy(admissions)
            changed[-1][field] = value
            cases.append(changed)
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                validate_admissions(rows, self.bindings)
        with self.assertRaises(ValueError):
            validate_admissions([], [])
