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
        const auto& a = t.vertices[s];
        const auto& b = t.vertices[s + 1];
        const auto& c = t.vertices[s + 2];
        if ((double(b.x) - a.x) * (c.y - a.y) == (double(b.y) - a.y) * (c.x - a.x)) continue;
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
    return triangulateCoverage(decodeTriangleEdges(encode(v, slopeBits)));
}

// RasterVS adds half an output pixel. D3D then rounds vertex coordinates to
// its 8-bit subpixel grid before testing coverage. Keep output pixel units so
// the sample positions themselves never need a lossy inverse transform.
EdgeTriangles snapVertices(EdgeTriangles t, double scale) {
    for (uint32_t i = 0; i < t.vertexCount; i++) {
        auto& p = t.vertices[i];
        p.x = float(std::round((p.x * scale + 0.5) * 256) / 256);
        p.y = float(std::round((p.y * scale + 0.5) * 256) / 256);
    }
    return t;
}

int main() {
    // Continuous M/L edges must keep one minor vertex at YM even when their
    // slopes need different guards. Also preserve an intentional source jog.
    for (double side : {-1.0, 1.0}) {
        for (double jog : {0.0, 0.25, -0.25}) {
            const TriangleEdges join{.leftMajor = side > 0, .yh = 0, .ym = 4, .yl = 8,
                .xh = 20, .xm = 20 + side * 2, .xl = 20 + side * 10 + jog,
                .dxhdy = 0, .dxmdy = side * 2, .dxldy = -side * .5};
            const auto mesh = triangulateCoverage(join);
            require(mesh.vertexCount == 12, "YM fixture did not emit both spans");
            // Upper minor bottom and lower minor top are repeated along each
            // trapezoid's diagonal. Check all occurrences, not just coverage.
            require(mesh.vertices[2].x == mesh.vertices[4].x, "Upper shared endpoint differs");
            require(mesh.vertices[2].y == 4 && mesh.vertices[7].y == 4, "YM endpoints moved");
            require(mesh.vertices[5].x == mesh.vertices[6].x && mesh.vertices[5].y == mesh.vertices[6].y,
                    "Major-edge endpoints do not meet at YM");
            require(double(mesh.vertices[7].x) - mesh.vertices[2].x == jog,
                    "Coverage guard introduced a new minor-edge step at YM");
        }
    }

    // Exactly coincident H/L coefficients, but splitting H at YM=87 moves
    // its new vertex off the neighbour's unsplit L after GPU snapping.
    // Before the fix, standard 4x MSAA sample (43.625,108.875), with a .75px
    // X shift, is uncovered
    // even at native resolution. Moving the geometry changes which frames
    // expose the crack. Also exercise the opposite major-edge orientation.
    for (bool mirror : {false, true}) {
        for (int shift = 0; shift < 8; shift++) {
            auto point = [&](double x, double y) -> Point {
                return {mirror ? 300 - x - shift * .25 : x + shift * .25, y};
            };
            auto a = decodeTriangleEdges(encode({point(10, 10), point(82.5, 230), point(2, 87)}, 10));
            auto b = decodeTriangleEdges(encode({point(100, 2), point(10, 10), point(82.5, 230)}, 10));
            // Keep the rounded slopes, including inverted corners. The old
            // test biased these slopes to avoid the split fallback, missing
            // the remaining cracks seen while racing.
            require(a.dxhdy == b.dxldy, "Snapping fixture edges have different slopes");
            const std::array<Point, 7> samples{{{.5,.5}, {.25,.25}, {.75,.75},
                {.375,.125}, {.875,.375}, {.125,.625}, {.625,.875}}};
            for (double scale : {1.0, 2.0, 4.8, 8.0}) {
                const auto left = snapVertices(triangulateCoverage(a), scale);
                const auto right = snapVertices(triangulateCoverage(b), scale);
                for (int row = int(12 * scale); row < int(228 * scale); row++) {
                    const int edgeColumn = int(std::floor(a.majorX(row / scale) * scale + .5));
                    for (int column = edgeColumn - 1; column <= edgeColumn + 1; column++) {
                        for (const auto& sample : samples) {
                            const double x = column + sample[0], y = row + sample[1];
                            // Stay close to the shared edge, clear of the
                            // quad's outer boundary near its narrow bottom.
                            if (std::abs(x - (a.majorX((y - .5) / scale) * scale + .5)) > .01) continue;
                            require(covers(left, x, y) || covers(right, x, y),
                                    "GPU vertex snapping opened a shared-edge crack");
                        }
                    }
                }
            }
        }
    }

    // A discontinuity at YM makes a concave polygon. Both directions of the
    // jog, and both major-edge orientations, must preserve the walked spans.
    for (bool leftMajor : {false, true}) {
        for (bool outward : {false, true}) {
            const double side = leftMajor ? 1 : -1;
            const TriangleEdges jog{.leftMajor = leftMajor, .yh = 0, .ym = 4, .yl = 8,
                .xh = 16, .xm = 16 + side * 6, .xl = 17 + side * (outward ? 8 : 2),
                .dxhdy = .25, .dxmdy = .25, .dxldy = .25};
            const auto polygon = triangulateCoverage(jog);
            require(polygon.vertexCount <= MaxEdgeVertices, "Edge polygon exceeded vertex storage");
            for (int iy = -4; iy < 36; iy++) {
                const double y = (iy + .5) / 4;
                for (int ix = 24; ix < 112; ix++) {
                    const double x = (ix + .5) / 4;
                    const double width = side * ((y < jog.ym ? jog.middleX(y) : jog.lowX(y)) - jog.majorX(y));
                    const double offset = side * (x - jog.majorX(y));
                    const bool expected = y >= jog.yh && y < jog.yl && offset >= 0 && offset <= width;
                    require(covers(polygon, x, y) == expected, "Minor-edge jog changed walked coverage");
                }
            }
        }
    }

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
    const std::vector<EdgeTriangles> measured{triangulateCoverage(left), triangulateCoverage(right)};
    // The right triangle's own H ends at x=299.62; keep 0.75px from the jog
    // at its bottom corner, where both RDP edges legitimately disagree.
    const std::array<Point, 4> measuredQuad{{{209, -39.75}, {494.25, -179.5}, {right.majorX(39.75), 39.75}, {249.25, 45.75}}};
    for (double scale : {1.0, 2.0, 4.8, 8.0}) {
        require(holes(measured, measuredQuad, scale, 0.75) == 0, "Measured BOSS pair leaves uncovered samples");
    }

    // Exact coefficients: production keeps all original MSAA coverage and
    // adds samples only within its documented horizontal guard band.
    const std::array<Point, 3> exact{{{8, 4}, {40, 36}, {2, 20}}};
    const auto edges = decodeTriangleEdges(encode(exact, 16));
    const auto split = triangulateCoverage(edges);
    EdgeTriangles original;
    for (unsigned i = 0; i < 3; i++) original.vertices[i] = {float(exact[i][0]), float(exact[i][1])};
    original.vertexCount = 3;
    const std::array<Point, 4> msaa{{{.375, .125}, {.875, .375}, {.125, .625}, {.625, .875}}};
    for (double scale : {1.0, 2.0, 3.0}) {
        for (int row = 0; row < 40 * scale; row++) {
            for (int column = 0; column < 48 * scale; column++) {
                for (const auto& s : msaa) {
                    const double x = (column + s[0]) / scale, y = (row + s[1]) / scale;
                    const bool wasCovered = covers(original, x, y);
                    const bool isCovered = covers(split, x, y);
                    require(!wasCovered || isCovered, "Guard removed original MSAA coverage");
                    const double nearest = std::min({distance({x, y}, exact[0], exact[1]),
                        distance({x, y}, exact[1], exact[2]), distance({x, y}, exact[2], exact[0])});
                    const double maxGuard = (1 + std::max({std::abs(edges.dxhdy), std::abs(edges.dxmdy), std::abs(edges.dxldy)})) / 256;
                    if (!wasCovered && nearest > maxGuard + 1e-4)
                        require(!isCovered, "Guard added coverage beyond its boundary band");
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

    // Production vertices stay within the guard band of the walked edges.
    for (uint32_t i = 0; i < split.vertexCount; i++) {
        const auto& v = split.vertices[i];
        require(v.y >= edges.yh && v.y <= edges.yl, "Vertex left the YH..YL span");
        const double h = edges.majorX(v.y), m = edges.middleX(v.y), l = edges.lowX(v.y);
        const double bound = (1 + std::max({std::abs(edges.dxhdy), std::abs(edges.dxmdy), std::abs(edges.dxldy)})) / 256 + 1e-4;
        require(std::abs(v.x - h) <= bound || (v.y <= edges.ym && std::abs(v.x - m) <= bound) ||
                (v.y >= edges.ym && std::abs(v.x - l) <= bound), "Vertex exceeded the guarded edge band");
    }

    // The RDP draws no span where the minor edge crosses to the wrong side of
    // H. Left-major H at x=10; M crosses it at y=10 and is inverted below.
    const TriangleEdges crossing{.leftMajor = true, .yh = 0, .ym = 20, .yl = 20, .xh = 10, .xm = 20, .xl = 0,
                                 .dxhdy = 0, .dxmdy = -1, .dxldy = 0};
    const auto cut = triangulateCoverage(crossing);
    require(covers(cut, 15, 2) && !covers(cut, 9, 15) && !covers(cut, 10.5, 12), "Inverted span was rasterized");

    // YM outside YH..YL uses only the edge pair the walker reaches.
    require(triangulateCoverage({.leftMajor = true, .yh = 5, .ym = 5, .yl = 5, .xh = 0, .xm = 0, .xl = 0,
                              .dxhdy = 1, .dxmdy = 1, .dxldy = 1}).vertexCount == 0, "Zero-height triangle emitted geometry");
    const TriangleEdges upper{.leftMajor = true, .yh = 0, .ym = 30, .yl = 10, .xh = 0, .xm = 10, .xl = 99,
                              .dxhdy = 0, .dxmdy = 0, .dxldy = 0};
    const auto upperOnly = triangulateCoverage(upper);
    require(covers(upperOnly, 5, 5) && !covers(upperOnly, 5, 11) && !covers(upperOnly, 50, 5), "YM beyond YL used the L edge");
    const auto& guarded = upperOnly;
    require(covers(guarded, -1.0 / 512, 5) && covers(guarded, 10 + 1.0 / 512, 5), "Coverage guard did not reach both boundaries");
    require(!covers(guarded, -1.0 / 128, 5) && !covers(guarded, 10 + 1.0 / 128, 5), "Coverage guard exceeded its subpixel bound");
    require(!covers(guarded, 5, -.001) && !covers(guarded, 5, 10.001), "Coverage guard changed Y endpoints");
    auto inactiveSlope = upper;
    inactiveSlope.dxldy = 1000000;
    const auto inactiveGuard = triangulateCoverage(inactiveSlope);
    require(inactiveGuard.vertexCount == guarded.vertexCount, "Inactive L slope changed geometry count");
    for (unsigned i = 0; i < guarded.vertexCount; ++i)
        require(inactiveGuard.vertices[i].x == guarded.vertices[i].x && inactiveGuard.vertices[i].y == guarded.vertices[i].y,
                "Inactive L slope enlarged the guard");
    auto empty = upper;
    empty.xm = empty.xh;
    require(triangulateCoverage(empty).vertexCount == 0, "Coverage guard created geometry from an empty span");
    // With an interior YM both production spans are collinear and empty.
    // Neither span may be expanded into visible geometry.
    empty.ym = 5;
    empty.xl = empty.xh;
    require(triangulateCoverage(empty).vertexCount == 0, "Coverage guard expanded collinear spans");
    empty.dxhdy = empty.dxmdy = empty.dxldy = 2;
    empty.xl = empty.majorX(empty.ym);
    require(triangulateCoverage(empty).vertexCount == 0, "Coverage guard expanded sloped collinear spans");
    const TriangleEdges emptyUpper{.leftMajor = true, .yh = 0, .ym = 5, .yl = 10,
        .xh = 0, .xm = -.001, .xl = 10, .dxhdy = 0, .dxmdy = 0, .dxldy = 0};
    const auto lowerGuard = triangulateCoverage(emptyUpper);
    for (unsigned i = 0; i < lowerGuard.vertexCount; ++i)
        require(lowerGuard.vertices[i].y >= 5, "Coverage guard revived an empty upper span");
    const auto guardedCut = triangulateCoverage(crossing);
    for (unsigned i = 0; i < guardedCut.vertexCount; ++i)
        require(guardedCut.vertices[i].y <= 10, "Coverage guard moved the original intersection cut");
    // The geometry is guarded, but the decoder evaluates attributes against
    // the original command's H edge. An added vertex must retain that offset.
    const auto guardOffset = planeOffset(upper, guarded.vertices[0]);
    require(guardOffset.dx == -1.0f / 256, "Coverage guard moved the attribute plane");
    const TriangleEdges lower{.leftMajor = true, .yh = 4, .ym = 0, .yl = 10, .xh = 0, .xm = 99, .xl = 10,
                              .dxhdy = 0, .dxmdy = 0, .dxldy = 0};
    const auto lowerOnly = triangulateCoverage(lower);
    require(covers(lowerOnly, 5, 5) && !covers(lowerOnly, 5, 3) && !covers(lowerOnly, 50, 5), "YM above YH used the M edge");

    std::puts("RDP triangle edge reconstruction regression passed");
}
