"""ROM-free checks for the RDP seam analyser (tools/rdp)."""
import math
import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'rdp'))
from rdp_triangles import HEADER, gap_samples, read_capture, shared_edges, triangles


def encode(vertices, slope_bits=16):
    """Encode a flat triangle as an RDP edge walker would receive it.

    Slopes are rounded to `slope_bits` fractional bits, as a microcode with a
    limited-precision reciprocal would; positions use the full s15.16 range.
    """
    top, mid, bottom = sorted(((round(x * 65536) / 65536, round(y * 4) / 4) for x, y in vertices),
                              key=lambda v: v[1])
    def slope(a, b):
        return 0.0 if b[1] == a[1] else round((b[0] - a[0]) / (b[1] - a[1]) * (1 << slope_bits)) / (1 << slope_bits)
    dxh, dxm, dxl = slope(top, bottom), slope(top, mid), slope(mid, bottom)
    floor_y = math.floor(top[1])
    xh = top[0] + dxh * (floor_y - top[1])
    xm = top[0] + dxm * (floor_y - top[1])
    # Left-major when the middle vertex lies right of the major edge.
    h_at_mid = top[0] + dxh * (mid[1] - top[1])
    left = mid[0] > h_at_mid
    fixed = lambda v: int(round(v * 65536)) & 0xFFFFFFFF
    q = lambda y: int(y * 4) & 0x3FFF
    words = [(0x08 << 24) | (0x800000 if left else 0) | q(bottom[1]), (q(mid[1]) << 16) | q(top[1]),
             fixed(mid[0]), fixed(dxl), fixed(xh), fixed(dxh), fixed(xm), fixed(dxm)]
    return words


def capture(*tris):
    scissor = [(0x2D << 24), (320 * 4 << 12) | 240 * 4]
    words = scissor + [w for t in tris for w in t]
    return HEADER + struct.pack('>%dI' % len(words), *words)


class SeamAnalyserTests(unittest.TestCase):
    # Two triangles sharing the long diagonal (10, 10)-(200, 230). The edge is
    # H for the left triangle and L for the right one, whose apex is higher.
    quad = ([(10, 10), (200, 230), (12, 225)], [(100, 2), (10, 10), (200, 230)])

    def decode(self, slope_bits):
        data = capture(*(encode(t, slope_bits) for t in self.quad))
        tris = list(triangles(read_capture(data)))
        self.assertEqual(len(tris), 2)
        return tris

    def test_decodes_edge_coefficients(self):
        tris = self.decode(16)
        self.assertEqual(tris[0].scissor, (0, 0, 320, 240))
        self.assertEqual((tris[0].yh, tris[0].yl), (10, 230))
        self.assertAlmostEqual(tris[0].h(230), 200, places=2)
        # Both spans lie left of their own H edge; the second triangle's
        # interior is right of its L edge, the shared diagonal.
        self.assertEqual(tris[0].interior_side(), -1)
        self.assertEqual(tris[1].interior_side(), -1)

    def test_shared_diagonal_is_paired(self):
        pairs = list(shared_edges(self.decode(16)))
        self.assertEqual(len(pairs), 1)
        self.assertEqual((pairs[0].left_edge, pairs[0].right_edge), ('H', 'L'))

    def test_coarse_slopes_open_rt64_seam_but_not_rdp_edges(self):
        # A 10-bit reciprocal leaves each H extrapolation up to ~0.2px from
        # the shared vertex; the RDP's own edges still coincide.
        (pair,) = shared_edges(self.decode(10))
        rt64_samples, rt64_gap = gap_samples(pair, 'rt64', 4.8)
        rdp_samples, rdp_gap = gap_samples(pair, 'rdp', 4.8)
        self.assertGreater(rt64_samples, 0)
        self.assertGreater(rt64_gap, 0.01)
        self.assertEqual(rdp_samples, 0)
        self.assertLess(rdp_gap, 0.01)


if __name__ == '__main__':
    unittest.main()
