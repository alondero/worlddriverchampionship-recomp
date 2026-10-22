"""Decode raw RDP triangle commands and measure seams between neighbours.

Input is a WDC_RDP_CAPTURE stream: an 8-byte "WDCRDP01" header followed by
big-endian 32-bit command words exactly as BOSS submitted them. Coordinates
below are framebuffer pixels. Edge semantics follow the RDP edge walker: the
H (major) edge spans YH..YL, the M edge YH..YM and the L edge YM..YL. XH and
XM are given at the scanline floor(YH); XL is given at YM.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass

HEADER = b"WDCRDP01"


def command_words(w0: int) -> int:
    """Length in 64-bit words; mirrors wdc::hud::command_words."""
    op = (w0 >> 24) & 63
    if 8 <= op <= 15:
        return 4 + (8 if op & 4 else 0) + (8 if op & 2 else 0) + (2 if op & 1 else 0)
    return 2 if op in (36, 37) else 1


def s32(value: int) -> int:
    return value - (1 << 32) if value & 0x80000000 else value


def s14(value: int) -> int:
    value &= 0x3FFF
    return value - (1 << 14) if value & 0x2000 else value


@dataclass
class Triangle:
    index: int
    op: int
    left_major: bool
    yh: float
    ym: float
    yl: float
    xh: float
    xm: float
    xl: float
    dxhdy: float
    dxmdy: float
    dxldy: float
    scissor: tuple
    color_image: int

    @property
    def y_floor(self) -> float:
        return math.floor(self.yh)

    def h(self, y: float) -> float:
        return self.xh + self.dxhdy * (y - self.y_floor)

    def m(self, y: float) -> float:
        return self.xm + self.dxmdy * (y - self.y_floor)

    def l(self, y: float) -> float:
        return self.xl + self.dxldy * (y - self.ym)

    @property
    def ymid(self) -> float:
        return min(max(self.ym, self.yh), self.yl)

    def rt64_vertices(self):
        """Pinned RT64 GBI_RDP::decodeTriangles: two H points and (XL, YM)."""
        return ((self.h(self.yh), self.yh), (self.h(self.yl), self.yl), (self.xl, self.ym))

    def edges(self, model: str):
        """Return {name: (x0, y0, x1, y1)} with y0 <= y1 for one model."""
        if model == "rt64":
            v1, v2, v3 = self.rt64_vertices()
            return {"H": (*v1, *v2), "M": (*v1, *v3), "L": (*v3, *v2)}
        ym = self.ymid
        return {"H": (self.h(self.yh), self.yh, self.h(self.yl), self.yl),
                "M": (self.m(self.yh), self.yh, self.m(ym), ym),
                "L": (self.l(ym), ym, self.l(self.yl), self.yl)}

    def closure(self):
        """Distances, in pixels, where the RDP's edges fail to meet.

        Returns (top, middle, bottom): |H - M| at YH, |M - XL| at YM and
        |H - L| at YL. RT64's three points ignore M and L, so these are the
        amounts by which its shape departs from the rasterized edges.
        """
        ym = self.ymid
        return (abs(self.h(self.yh) - self.m(self.yh)) if ym > self.yh else 0.0,
                abs(self.m(ym) - self.l(ym)) if self.yh < ym < self.yl else 0.0,
                abs(self.h(self.yl) - self.l(self.yl)) if ym < self.yl else 0.0)

    def interior_side(self) -> int:
        """+1 when spans extend right of H, else -1 (measured, not the flag)."""
        y = (self.yh + self.yl) / 2
        minor = self.m(y) if y < self.ymid else self.l(y)
        return 1 if minor > self.h(y) else -1


def read_capture(data: bytes):
    if not data.startswith(HEADER) or (len(data) - len(HEADER)) % 8:
        raise ValueError("not a WDC_RDP_CAPTURE stream")
    words = struct.unpack_from(">%dI" % ((len(data) - len(HEADER)) // 4), data, len(HEADER))
    return [(words[i], words[i + 1]) for i in range(0, len(words), 2)]


def triangles(commands):
    """Yield decoded triangles with the scissor and color image active at the time."""
    scissor = (0.0, 0.0, 0.0, 0.0)
    color_image = 0
    i = 0
    while i < len(commands):
        w0, w1 = commands[i]
        op = (w0 >> 24) & 63
        length = command_words(w0)
        if op == 0x2D:
            scissor = (((w0 >> 12) & 0xFFF) / 4, (w0 & 0xFFF) / 4, ((w1 >> 12) & 0xFFF) / 4, (w1 & 0xFFF) / 4)
        elif op == 0x3F:
            color_image = w1 & 0xFFFFFF
        elif 8 <= op <= 15 and i + 4 <= len(commands):
            c = commands[i:i + 4]
            yield Triangle(
                index=i, op=op, left_major=bool(w0 & 0x00800000),
                yl=s14(w0) / 4, ym=s14(w1 >> 16) / 4, yh=s14(w1) / 4,
                xl=s32(c[1][0]) / 65536, dxldy=s32(c[1][1]) / 65536,
                xh=s32(c[2][0]) / 65536, dxhdy=s32(c[2][1]) / 65536,
                xm=s32(c[3][0]) / 65536, dxmdy=s32(c[3][1]) / 65536,
                scissor=scissor, color_image=color_image)
        i += length


def _x_at(edge, y):
    x0, y0, x1, y1 = edge
    return x0 if y1 == y0 else x0 + (x1 - x0) * (y - y0) / (y1 - y0)


@dataclass
class SharedEdge:
    left: Triangle
    left_edge: str
    right: Triangle
    right_edge: str


def shared_edges(tris, tolerance=0.25):
    """Pair triangles whose RDP-defined edges coincide with opposite interiors.

    Matching uses the edge-faithful geometry, which is what the RDP rasterizes.
    Horizontal and zero-height edges are ignored; T-junctions are not paired.
    """
    buckets = {}
    for tri in tris:
        side = tri.interior_side()
        for name, (x0, y0, x1, y1) in tri.edges("rdp").items():
            if y1 - y0 < 0.5:
                continue
            # The minor edges bound the opposite side of the span from H.
            interior = side if name == "H" else -side
            key = (tri.color_image, y0, y1)
            buckets.setdefault(key, []).append((tri, name, x0, x1, interior))
    for entries in buckets.values():
        for a in range(len(entries)):
            for b in range(a + 1, len(entries)):
                ta, na, ax0, ax1, ia = entries[a]
                tb, nb, bx0, bx1, ib = entries[b]
                if ta is tb or ia == ib:
                    continue
                if abs(ax0 - bx0) > tolerance or abs(ax1 - bx1) > tolerance:
                    continue
                # The left neighbour's interior lies at smaller x.
                if ia < 0:
                    yield SharedEdge(ta, na, tb, nb)
                else:
                    yield SharedEdge(tb, nb, ta, na)


def gap_samples(pair: SharedEdge, model: str, scale: float):
    """Count GPU sample centres left uncovered between two neighbours.

    Samples sit at (k + 0.5) / scale in framebuffer pixels on rows inside both
    edges' y range and the active scissor. Returns (samples, max_gap_pixels).
    """
    left = pair.left.edges(model)[pair.left_edge]
    right = pair.right.edges(model)[pair.right_edge]
    y0 = max(left[1], right[1], pair.left.scissor[1])
    y1 = min(left[3], right[3], pair.left.scissor[3])
    x_min, x_max = pair.left.scissor[0], pair.left.scissor[2]
    count, widest = 0, 0.0
    row = math.floor(y0 * scale)
    while (row + 0.5) / scale < y1:
        y = (row + 0.5) / scale
        row += 1
        if y <= y0:
            continue
        xl, xr = _x_at(left, y), _x_at(right, y)
        widest = max(widest, xr - xl)
        lo, hi = max(xl, x_min), min(xr, x_max)
        if hi > lo:
            # Sample centres with lo <= x < hi.
            count += max(0, math.ceil(hi * scale - 0.5) - math.ceil(lo * scale - 0.5))
    return count, widest
