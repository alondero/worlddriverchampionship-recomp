"""ROM-free checks for measured track-list instruction relocation."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from gen_syms_toml import visibility_list_patches

OLD = 0x800B2880
NEW = 0x807F2880
ENTRY = 0x80000400
SECTION_ROM = 0x1000


def lui(reg: int, immediate: int) -> int:
    return 0x3C000000 | (reg << 16) | immediate


def addiu(target: int, source: int, immediate: int) -> int:
    return 0x24000000 | (source << 21) | (target << 16) | immediate


def sw(source: int, base: int, immediate: int) -> int:
    return 0xAC000000 | (base << 21) | (source << 16) | immediate


def rom_with(offset_words: dict[int, int], size: int = 0x2000) -> bytes:
    words = [0] * (size // 4)
    for offset, word in offset_words.items():
        words[offset // 4] = word
    return struct.pack('>' + 'I' * len(words), *words)


class VisibilityRelocationTests(unittest.TestCase):
    def test_full_constant_and_memory_reference_luis_are_relocated(self):
        # ROM 0x1000 maps to ENTRY. One function forms a full pointer; the
        # other uses the high register as a memory base. The scratch low half
        # 0x2980 is deliberately present and must not be patched.
        rom = rom_with({
            0x1000: lui(2, 0x800B),
            0x1004: addiu(2, 2, 0x2880),
            0x1008: lui(6, 0x800B),
            0x100C: addiu(6, 6, 0x2884),
            0x1010: lui(3, 0x800B),
            0x1014: addiu(3, 3, 0x2980),
            0x1100: lui(4, 0x800B),
            0x1104: sw(5, 4, 0x2880),
        })
        functions = [("func_8002343C", ENTRY, 0x20), ("func_800218F0", ENTRY + 0x100, 0x20)]
        patches = visibility_list_patches(rom, functions, ENTRY, SECTION_ROM, OLD, NEW)
        self.assertEqual([patch["vram"] for patch in patches], [ENTRY, ENTRY + 8, ENTRY + 0x100])
        self.assertEqual(patches[0]["expected"], lui(2, 0x800B))
        self.assertEqual(patches[0]["value"], lui(2, 0x807F))
        self.assertEqual(patches[1]["value"], lui(6, 0x807F))
        self.assertEqual(patches[2]["value"], lui(4, 0x807F))

    def test_every_consumer_function_must_have_a_measured_site(self):
        rom = rom_with({0x1000: lui(2, 0x800B), 0x1004: addiu(2, 2, 0x2880)})
        functions = [("func_8002343C", ENTRY, 0x20), ("func_800218F0", ENTRY + 0x100, 0x20)]
        with self.assertRaisesRegex(ValueError, "func_800218F0"):
            visibility_list_patches(rom, functions, ENTRY, SECTION_ROM, OLD, NEW)

    def test_relocation_keeps_the_low_half_so_only_lui_is_patched(self):
        rom = rom_with({0x1000: lui(2, 0x800B), 0x1004: addiu(2, 2, 0x2880)})
        functions = [("func_8002343C", ENTRY, 0x20), ("func_800218F0", ENTRY, 0x20)]
        with self.assertRaisesRegex(ValueError, "low 16 bits"):
            visibility_list_patches(rom, functions, ENTRY, SECTION_ROM, OLD, 0x807F2000)


if __name__ == '__main__':
    unittest.main()
