"""Caller-sensitive hooks must see MIPS JAL links, including disallowed callers."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from gen_syms_toml import link_register_hooks


BASE = 0x80000000
FIRST = BASE + 0x80
SECOND = BASE + 0x90


def instruction(target, opcode=3):
    return opcode << 26 | (target >> 2 & 0x03FFFFFF)


class CheatCallerTests(unittest.TestCase):
    def setUp(self):
        self.rom = bytearray(0x100)
        self.functions = [("menu", BASE, 0x40), ("save", BASE + 0x40, 0x40),
                          ("first", FIRST, 0x10), ("second", SECOND, 0x10)]

    def put(self, offset, value):
        struct.pack_into(">I", self.rom, offset, value)

    def hooks(self, targets=(FIRST,), existing=()):
        return link_register_hooks(bytes(self.rom), list(targets), self.functions,
                                   0, 0xA0, BASE, existing)

    def test_menu_and_save_both_replace_a_stale_link(self):
        self.put(0x10, instruction(FIRST))
        self.put(0x50, instruction(FIRST))
        self.put(0x54, 0x03E02025)  # delay slot: move a0, ra
        self.put(0x20, instruction(SECOND))  # unrelated callee stays untouched
        original = bytes(self.rom)
        hooks = self.hooks()
        self.assertEqual(hooks, [
            {"func": "menu", "before_vram": BASE + 0x10,
             "text": "ctx->r31 = (int32_t)0x80000018u;"},
            {"func": "save", "before_vram": BASE + 0x50,
             "text": "ctx->r31 = (int32_t)0x80000058u;"},
        ])
        self.assertEqual(bytes(self.rom), original)
        # Inserting before JAL reproduces its link even when the delay slot
        # itself reads ra; putting this hook after the call would be too late.
        self.assertEqual(hooks[1]["before_vram"], BASE + 0x50)

    def test_every_selected_callee_and_overlapping_body_gets_a_link(self):
        self.put(0x10, instruction(FIRST))
        self.put(0x50, instruction(SECOND))
        self.functions.append(("shared_entry", BASE + 0x10, 0x30))
        hooks = self.hooks((FIRST, SECOND))
        self.assertEqual({(h["func"], h["before_vram"]) for h in hooks}, {
            ("menu", BASE + 0x10), ("shared_entry", BASE + 0x10),
            ("save", BASE + 0x50),
        })

    def test_raw_function_pointer_requires_explicit_indirect_policy(self):
        self.put(0x10, instruction(FIRST))
        self.put(0xC0, FIRST)
        with self.assertRaisesRegex(ValueError, "function pointer at ROM 0xc0"):
            self.hooks()

    def test_tail_jump_cannot_reuse_an_allowlisted_link(self):
        self.put(0x10, instruction(FIRST))
        self.put(0x50, instruction(FIRST, opcode=2))
        with self.assertRaisesRegex(ValueError, "tail entry.*0x80000050"):
            self.hooks()

    def test_call_and_delay_slot_must_be_inside_each_owner(self):
        self.put(0x3C, instruction(FIRST))
        with self.assertRaisesRegex(ValueError, "complete function bodies"):
            self.hooks()

    def test_call_cannot_end_the_measured_code_without_delay_slot(self):
        self.put(0x9C, instruction(FIRST))
        with self.assertRaisesRegex(ValueError, "lacks its delay slot"):
            self.hooks()

    def test_conflicting_hook_requires_review(self):
        self.put(0x10, instruction(FIRST))
        with self.assertRaisesRegex(ValueError, "conflicts with menu"):
            self.hooks(existing=[{"func": "menu", "before_vram": BASE + 0x10}])
        self.assertEqual(len(self.hooks(existing=[{
            "func": "save", "before_vram": BASE + 0x10}])), 1)

    def test_missing_or_uncalled_target_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "no measured direct callers"):
            self.hooks()
        with self.assertRaisesRegex(ValueError, "function entry"):
            self.hooks((BASE + 0xA0,))
        self.assertEqual(self.hooks(()), [])

    def test_jump_target_uses_pc_plus_four_region(self):
        rom = struct.pack(">II", instruction(0x90000010), 0)
        hooks = link_register_hooks(rom, [0x90000010], [
            ("boundary_caller", 0x8FFFFFFC, 8), ("callee", 0x90000010, 4)],
            0, len(rom), 0x8FFFFFFC)
        self.assertEqual(hooks[0]["text"], "ctx->r31 = (int32_t)0x90000004u;")


if __name__ == "__main__":
    unittest.main()
