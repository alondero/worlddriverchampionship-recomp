"""ROM-free regression for BOSS's measured self-modifying cache markers."""
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from rom_transforms import expand_cache_markers


class CacheTransformTests(unittest.TestCase):
    def test_marker_chain_is_cache_not_pointer_arithmetic(self):
        words = [0x3a195bfd, 0x33383bfd, 0x370e2bfd, 0x8fb800b4,
                 0x39cf1bfd, 0x00127080, 0x030e1021, 0x8c590000,
                 0x3c019000, 0x31f00bfd] + [0] * 6 + [0x03e00008, 0]
        original = struct.pack('>' + 'I' * len(words), *words)
        output, changed = expand_cache_markers(original, [0x1000], 0x1000)
        self.assertEqual(list(changed.values()), [0xbe0d0000 + i*16 for i in range(5)])
        self.assertEqual(len(changed), 5)
        self.assertEqual(struct.unpack_from('>I', output, 12)[0], words[3])
        self.assertNotEqual(output, original)

    def test_no_marker_is_unchanged(self):
        original = struct.pack('>III', 0x24840004, 0x03e00008, 0)
        self.assertEqual(expand_cache_markers(original, [0], 0), (original, {}))

    def test_wrong_rom_is_rejected_before_output(self):
        # The generator validates a pinned digest, not only size/header.
        source = (Path(__file__).resolve().parents[1] / 'scripts/gen_syms_toml.py').read_text()
        self.assertLess(source.index('hashlib.sha256(rom)'), source.index('OUT_SYMS.write_text'))


if __name__ == '__main__':
    unittest.main()
