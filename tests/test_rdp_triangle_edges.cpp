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
    return edgeTriangles(decodeTriangleEdges(encode(v, slopeBits)));
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

    // Exact coefficients: the trapezoids cover exactly the original triangle's
    // standard four-sample MSAA positions at 1x-3x.
    const std::array<Point, 3> exact{{{8, 4}, {40, 36}, {2, 20}}};
    const auto edges = decodeTriangleEdges(encode(exact, 16));
    const auto split = edgeTriangles(edges);
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

    // Vertices lie on the walked edges; attributes are evaluated from the
    // major edge exactly as the original plane (dy from floor(YH), dx from H).
    for (uint32_t i = 0; i < split.vertexCount; i++) {
        const auto& v = split.vertices[i];
        require(v.y >= edges.yh && v.y <= edges.yl, "Vertex left the YH..YL span");
        const double h = edges.majorX(v.y), m = edges.middleX(v.y), l = edges.lowX(v.y);
        require(std::abs(v.x - h) < 1e-4 || (v.y <= edges.ym && std::abs(v.x - m) < 1e-4) ||
                (v.y >= edges.ym && std::abs(v.x - l) < 1e-4), "Vertex is not on an RDP edge");
    }

    // The RDP draws no span where the minor edge crosses to the wrong side of
    // H. Left-major H at x=10; M crosses it at y=10 and is inverted below.
    TriangleEdges crossing{true, 0, 20, 20, 10, 20, 0, 0, -1, 0};
    const auto cut = edgeTriangles(crossing);
    require(covers(cut, 15, 2) && !covers(cut, 9, 15) && !covers(cut, 10.5, 12), "Inverted span was rasterized");

    // YM outside YH..YL uses only the edge pair the walker reaches.
    require(edgeTriangles({true, 5, 5, 5, 0, 0, 0, 1, 1, 1}).vertexCount == 0, "Zero-height triangle emitted geometry");
    TriangleEdges upper{true, 0, 30, 10, 0, 10, 99, 0, 0, 0};
    const auto upperOnly = edgeTriangles(upper);
    require(covers(upperOnly, 5, 5) && !covers(upperOnly, 5, 11) && !covers(upperOnly, 50, 5), "YM beyond YL used the L edge");
    TriangleEdges lower{true, 4, 0, 10, 0, 99, 10, 0, 0, 0};
    const auto lowerOnly = edgeTriangles(lower);
    require(covers(lowerOnly, 5, 5) && !covers(lowerOnly, 5, 3) && !covers(lowerOnly, 50, 5), "YM above YH used the M edge");

    std::puts("RDP triangle edge reconstruction regression passed");
}
