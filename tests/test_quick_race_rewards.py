"""ROM-free checks for signed, interleaved Quick Race reward records."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rom"))
from quick_race_rewards import decode_rewards, reward_cells


class QuickRaceRewardTests(unittest.TestCase):
    def test_profiles_are_not_course_conditions_and_rewards_are_signed(self):
        table = bytearray(35 * 18)
        # Distinct profile selectors must not appear among reward cells.
        struct.pack_into(">9h", table, 0, 99, 111, -7, 222, 17, 333, 0, 444, 51)
        struct.pack_into(">9h", table, 34 * 18, 88, 9, 2, 8, -33, 7, 41, 6, 0)
        cells = reward_cells(table)
        self.assertEqual(cells[0], (-7, 17, 0, 51))
        self.assertEqual(cells[1], (0, 0, 0, 0))
        self.assertEqual(cells[34], (2, -33, 41, 0))

    def test_partial_or_extra_records_fail(self):
        for size in (0, 35 * 18 - 1, 35 * 18 + 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                reward_cells(bytes(size))

    def test_unreviewed_rom_fails_before_fixed_address_reads(self):
        with self.assertRaisesRegex(ValueError, "identity"):
            decode_rewards(bytes.fromhex("80371240"))


if __name__ == "__main__":
    unittest.main()
