#include "gbi/rt64_rdp_triangle_edges.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace RT64::GBI_RDP;
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
using Point = std::array<double, 2>;

// Encode a flat triangle as an RDP triangle command. Slopes keep
// `slopeBits` fractional bits, like a microcode with a limited-precision
// reciprocal; XH/XM are at floor(YH) and XL at YM, as the edge walker expects.
std::array<uint32_t, 8> encode(std::array<Point, 3> v, int slopeBits) {
    for (auto& p : v) { p[0] = std::round(p[0] * 65536) / 65536; p[1] = std::round(p[1] * 4) / 4; }
    std::sort(v.begin(), v.end(), [](const Point& a, const Point& b) { return a[1] < b[1]; });
    const auto& top = v[0]; const auto& mid = v[1]; const auto& bottom = v[2];
    auto slope = [&](const Point& a, const Point& b) {
        return b[1] == a[1] ? 0.0 : std::round((b[0] - a[0]) / (b[1] - a[1]) * (1 << slopeBits)) / (1 << slopeBits);
    };
    const double dxh = slope(top, bottom), dxm = slope(top, mid), dxl = slope(mid, bottom);
    const double yFloor = std::floor(top[1]);
    const bool left = mid[0] > top[0] + dxh * (mid[1] - top[1]);
    auto fixed = [](double x) { return uint32_t(int32_t(std::lround(x * 65536))); };
    auto q = [](double y) { return uint32_t(int32_t(y * 4)) & 0x3FFF; };
    return {(0x08u << 24) | (left ? 0x800000u : 0) | q(bottom[1]), (q(mid[1]) << 16) | q(top[1]),
            fixed(mid[0]), fixed(dxl), fixed(top[0] + dxh * (yFloor - top[1])), fixed(dxh),
            fixed(top[0] + dxm * (yFloor - top[1])), fixed(dxm)};
}

bool covers(const EdgeTriangles& t, double x, double y) {
    for (uint32_t s = 0; s < t.vertexCount; s += 3) {
        bool positive = false, negative = false;
        for (unsigned e = 0; e < 3; e++) {
            const auto& a = t.vertices[s + e]; const auto& b = t.vertices[s + (e + 1) % 3];
            const double cross = (double(b.x) - a.x) * (y - a.y) - (double(b.y) - a.y) * (x - a.x);
            positive |= cross > 0; negative |= cross < 0;
        }
        if (!(positive && negative)) return true;
    }
    return false;
}

// Distance from p to segment ab.
double distance(const Point& p, const Point& a, const Point& b) {
    const double dx = b[0] - a[0], dy = b[1] - a[1];
    const double t = std::clamp(((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
    return std::hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy);
}

// Count GPU sample centres inside a convex quad (at least `margin` pixels from
// its outline) that neither neighbour covers: the visible seam/pinhole case.
unsigned holes(const std::vector<EdgeTriangles>& tris, const std::array<Point, 4>& quad, double scale, double margin) {
    unsigned count = 0;
    for (int row = 0; row < 240 * scale; row++) {
        for (int column = 0; column < 320 * scale; column++) {
            const Point p{(column + 0.5) / scale, (row + 0.5) / scale};
            bool inside = true;
            for (unsigned e = 0; e < 4 && inside; e++) {
                const auto& a = quad[e]; const auto& b = quad[(e + 1) % 4];
                const double cross = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
                inside = cross > 0 && distance(p, a, b) > margin;
            }
            if (inside && std::none_of(tris.begin(), tris.end(), [&](const auto& t) { return covers(t, p[0], p[1]); })) count++;
        }
    }
    return count;
}

EdgeTriangles reconstruct(const std::array<Point, 3>& v, int slopeBits) {
    return triangulateEdges(decodeTriangleEdges(encode(v, slopeBits)));
}

int main() {
    // Synthetic quad, no captured ROM commands. The diagonal (10,10)-(200,230)
    // is the H edge of the left triangle and the L edge of the right one. With
    // a 10-bit slope each H extrapolation misses the shared vertex by up to
    // ~0.1px, so a three-point reconstruction opens a seam the RDP never draws.
    const std::array<Point, 4> quad{{{10, 10}, {100, 2}, {200, 230}, {12, 225}}};
    const std::vector<EdgeTriangles> pair{reconstruct({{{10, 10}, {200, 230}, {12, 225}}}, 10),
                                          reconstruct({{{100, 2}, {10, 10}, {200, 230}}}, 10)};
    for (double scale : {1.0, 2.0, 4.8, 8.0}) {
        require(holes(pair, quad, scale, 0.25) == 0, "Adjacent raw RDP triangles leave uncovered samples");
    }

    // Reduced from a failing Hawaii pair (coefficients rounded to 1/1024 px;
    // no command words retained). Their shared edge is the right triangle's L
    // and the left triangle's M, with identical slopes. The right triangle's
    // H starts off-screen and its slope is ~0.3% off, so extrapolating H to YL
    // misses the shared vertex (299, 39.75) by 0.64px.
    const TriangleEdges right{.leftMajor = false, .yh = -179.5, .ym = -39.75, .yl = 39.75,
        .xh = 494.6943359375, .xm = 495.2685546875, .xl = 209.0,
        .dxhdy = -0.8876953125, .dxmdy = -2.037109375, .dxldy = 1.1318359375};
    const TriangleEdges left{.leftMajor = true, .yh = -39.75, .ym = 39.75, .yl = 45.75,
        .xh = 208.8828125, .xm = 208.716796875, .xl = 299.0,
        .dxhdy = 0.470703125, .dxmdy = 1.1318359375, .dxldy = -8.291015625};
    const std::vector<EdgeTriangles> measured{triangulateEdges(left), triangulateEdges(right)};
    // The right triangle's own H ends at x=299.62; keep 0.75px from the jog
    // at its bottom corner, where both RDP edges legitimately disagree.
    const std::array<Point, 4> measuredQuad{{{209, -39.75}, {494.25, -179.5}, {right.majorX(39.75), 39.75}, {249.25, 45.75}}};
    for (double scale : {1.0, 2.0, 4.8, 8.0}) {
        require(holes(measured, measuredQuad, scale, 0.75) == 0, "Measured BOSS pair leaves uncovered samples");
    }

    // Exact coefficients: the trapezoids cover exactly the original triangle's
    // standard four-sample MSAA positions at 1x-3x.
    const std::array<Point, 3> exact{{{8, 4}, {40, 36}, {2, 20}}};
    const auto edges = decodeTriangleEdges(encode(exact, 16));
    const auto split = triangulateEdges(edges);
    EdgeTriangles original;
    for (unsigned i = 0; i < 3; i++) original.vertices[i] = {float(exact[i][0]), float(exact[i][1])};
    original.vertexCount = 3;
    const std::array<Point, 4> msaa{{{.375, .125}, {.875, .375}, {.125, .625}, {.625, .875}}};
    for (double scale : {1.0, 2.0, 3.0}) {
        for (int row = 0; row < 40 * scale; row++) {
            for (int column = 0; column < 48 * scale; column++) {
                for (const auto& s : msaa) {
                    const double x = (column + s[0]) / scale, y = (row + s[1]) / scale;
                    require(covers(split, x, y) == covers(original, x, y), "Exact triangle changed MSAA sample coverage");
                }
            }
        }
    }

    // Attribute planes: offsets at the pinned decoder's three corners match its
    // formulas (on H: dx = 0; at (XL, YM): dx = XL - H(YM)). A plane sampled
    // at the new vertices and interpolated inside each triangle reproduces
    // the plane itself, so shade/texture/depth gradients are unchanged.
    const auto top = planeOffset(edges, {float(edges.majorX(edges.yh)), float(edges.yh)});
    const auto middle = planeOffset(edges, {float(edges.xl), float(edges.ym)});
    require(top.dy == float(edges.yh - edges.yFloor()) && top.dx == 0, "Major-edge plane offset changed");
    require(std::abs(middle.dx - (edges.xl - edges.majorX(edges.ym))) < 1e-5, "Minor-vertex plane offset changed");
    auto shade = [&](double x, double y) {
        const auto o = planeOffset(edges, {float(x), float(y)});
        return 0.25 + 0.0625 * o.dy - 0.03125 * o.dx;
    };
    for (uint32_t s = 0; s < split.vertexCount; s += 3) {
        const auto& a = split.vertices[s]; const auto& b = split.vertices[s + 1]; const auto& c = split.vertices[s + 2];
        const double area = (double(b.x) - a.x) * (c.y - a.y) - (double(b.y) - a.y) * (c.x - a.x);
        if (std::abs(area) < 1e-9) continue;
        const double x = (a.x + b.x + c.x) / 3.0, y = (a.y + b.y + c.y) / 3.0;
        const double interpolated = (shade(a.x, a.y) + shade(b.x, b.y) + shade(c.x, c.y)) / 3.0;
        require(std::abs(interpolated - shade(x, y)) < 1e-5, "Shade gradient changed inside the edge area");
    }

    // Vertices lie on the walked edges.
    for (uint32_t i = 0; i < split.vertexCount; i++) {
        const auto& v = split.vertices[i];
        require(v.y >= edges.yh && v.y <= edges.yl, "Vertex left the YH..YL span");
        const double h = edges.majorX(v.y), m = edges.middleX(v.y), l = edges.lowX(v.y);
        require(std::abs(v.x - h) < 1e-4 || (v.y <= edges.ym && std::abs(v.x - m) < 1e-4) ||
                (v.y >= edges.ym && std::abs(v.x - l) < 1e-4), "Vertex is not on an RDP edge");
    }

    // The RDP draws no span where the minor edge crosses to the wrong side of
    // H. Left-major H at x=10; M crosses it at y=10 and is inverted below.
    const TriangleEdges crossing{.leftMajor = true, .yh = 0, .ym = 20, .yl = 20, .xh = 10, .xm = 20, .xl = 0,
                                 .dxhdy = 0, .dxmdy = -1, .dxldy = 0};
    const auto cut = triangulateEdges(crossing);
    require(covers(cut, 15, 2) && !covers(cut, 9, 15) && !covers(cut, 10.5, 12), "Inverted span was rasterized");

    // YM outside YH..YL uses only the edge pair the walker reaches.
    require(triangulateEdges({.leftMajor = true, .yh = 5, .ym = 5, .yl = 5, .xh = 0, .xm = 0, .xl = 0,
                              .dxhdy = 1, .dxmdy = 1, .dxldy = 1}).vertexCount == 0, "Zero-height triangle emitted geometry");
    const TriangleEdges upper{.leftMajor = true, .yh = 0, .ym = 30, .yl = 10, .xh = 0, .xm = 10, .xl = 99,
                              .dxhdy = 0, .dxmdy = 0, .dxldy = 0};
    const auto upperOnly = triangulateEdges(upper);
    require(covers(upperOnly, 5, 5) && !covers(upperOnly, 5, 11) && !covers(upperOnly, 50, 5), "YM beyond YL used the L edge");
    const TriangleEdges lower{.leftMajor = true, .yh = 4, .ym = 0, .yl = 10, .xh = 0, .xm = 99, .xl = 10,
                              .dxhdy = 0, .dxmdy = 0, .dxldy = 0};
    const auto lowerOnly = triangulateEdges(lower);
    require(covers(lowerOnly, 5, 5) && !covers(lowerOnly, 5, 3) && !covers(lowerOnly, 50, 5), "YM above YH used the M edge");

    std::puts("RDP triangle edge reconstruction regression passed");
}
