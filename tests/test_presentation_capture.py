import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


SCRIPT = Path(__file__).resolve().parents[1] / "tools/render_split_prototype/compare_captures.py"


class PresentationCaptureTest(unittest.TestCase):
    def test_matched_draw_uses_labels_instead_of_presentation_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline, candidate = root / "before", root / "after"
            for run, capture, pixel in ((baseline, "present-12-0.ppm", b"\x10\x20\x30"),
                                        (candidate, "present-70-0.ppm", b"\x12\x10\x30")):
                run.mkdir()
                (run / "captures.json").write_text(json.dumps([
                    {"frame": 22, "draw": 0, "file": "unselected.ppm"},
                    {"frame": 22, "draw": 1, "file": capture},
                ]), encoding="utf-8")
                (run / capture).write_bytes(b"P6\n2 1\n255\n" + b"\xf0\xf0\xf0" + pixel)
            output = root / "diff.png"
            command = [sys.executable, "-B", str(SCRIPT), str(baseline), str(candidate),
                       "--frame", "22", "--draw", "1", "--crop", "1", "0", "1", "1",
                       "--output", str(output)]
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            report = json.loads(result.stdout)
            self.assertEqual(report["changed_pixels"], 1)
            self.assertEqual(report["max_channel_delta"], 16)
            png = output.read_bytes()
            self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack_from(">II", png, 16), (3, 1))
            length = struct.unpack_from(">I", png, 33)[0]
            self.assertEqual(png[37:41], b"IDAT")
            self.assertEqual(zlib.decompress(png[41:41 + length]),
                             b"\x00\x10\x20\x30\x12\x10\x30\x02\x10\x00")

            command[command.index("--draw") + 1] = "2"
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("found 0", result.stderr)
