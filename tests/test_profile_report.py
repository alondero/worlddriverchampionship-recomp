"""Check the BOSS profiler's report without a build or a profile.

tools/boss/profile_report.py turns the bench's instruction-pointer samples
into RSP labels. The parts that decide what a sample means are checked here:
reading the sample file, reading the generated labels, reading addr2line's
output, and grouping the samples.
"""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "boss"))

import profile_report  # noqa: E402  (the path is set above)


def write_pe(path: Path, base: int) -> None:
    """Write a minimal PE32+ header, enough for the ImageBase lookup."""
    data = bytearray(0x200)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", data, 0x80 + 24, 0x20B)      # optional header magic
    struct.pack_into("<Q", data, 0x80 + 24 + 24, base)  # ImageBase
    path.write_bytes(bytes(data))


class ProfileReportTest(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)

    def tearDown(self) -> None:
        self.dir.cleanup()

    def test_samples_are_read_as_offsets_and_counts(self) -> None:
        path = self.root / "samples.txt"
        path.write_text("# wdc_boss_bench samples\n1000 3\n20a 1\n", encoding="utf-8")
        self.assertEqual(profile_report.read_samples(path), {0x1000: 3, 0x20A: 1})

    def test_a_malformed_sample_line_is_rejected(self) -> None:
        path = self.root / "samples.txt"
        path.write_text("1000 3\n20a\n", encoding="utf-8")
        with self.assertRaises(SystemExit):
            profile_report.read_samples(path)

    def test_labels_are_read_with_their_line_numbers(self) -> None:
        asp = self.root / "aspMain.cpp"
        asp.write_text("// header\nL_1080:\n    work\nL_10A0:\n", encoding="utf-8")
        lines, labels = profile_report.label_index(asp)
        self.assertEqual(lines, [2, 4])
        self.assertEqual(labels, [0x1080, 0x10A0])

    def test_addr2line_output_is_split_into_frames(self) -> None:
        output = ("0x140001000\ndma_dmem_to_rdram\nsrc/aspMain.cpp:3312\n"
                  "bossMain\nsrc/aspMain.cpp:3308\n"
                  "0x140002000\n??\n??:0\n")
        frames = profile_report.parse_frames(output)
        self.assertEqual(frames[0x140001000],
                         [("dma_dmem_to_rdram", "src/aspMain.cpp:3312"),
                          ("bossMain", "src/aspMain.cpp:3308")])
        self.assertEqual(frames[0x140002000], [("??", "??:0")])

    def test_the_image_base_comes_from_the_pe_header(self) -> None:
        exe = self.root / "bench.exe"
        write_pe(exe, 0x140000000)
        self.assertEqual(profile_report.image_base(exe), 0x140000000)

    def test_samples_are_grouped_by_label_and_window(self) -> None:
        base = 0x140000000
        samples = {0x1000: 3, 0x2000: 1}
        frames = {
            base + 0x1000: [("dma_dmem_to_rdram", "src/aspMain.cpp:3312"),
                            ("bossMain", "src/aspMain.cpp:3308")],
            base + 0x2000: [("memcpy", "src/other.cpp:10")],
        }
        labels = profile_report.LabelIndex([2, 4], [0x1080, 0x10A0])
        by_label, by_window, by_leaf = profile_report.attribute(
            samples, frames, base, labels, 0x40)
        # The generated line 3312 falls after both labels, so it is the last.
        self.assertEqual(by_label["L_10A0"], 3)
        self.assertEqual(by_window["IMEM 1080-10BF"], 3)
        self.assertEqual(by_leaf["dma_dmem_to_rdram"], 3)
        # A sample outside the generated code is reported as such.
        self.assertEqual(by_label["(outside bossMain: memcpy)"], 1)
        self.assertEqual(by_leaf["memcpy"], 1)


if __name__ == "__main__":
    unittest.main()
