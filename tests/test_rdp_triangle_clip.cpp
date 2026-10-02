#include "gbi/rt64_rdp_triangle_clip.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace RT64::GBI_RDP;
void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
bool near(double a, double b) { return std::abs(a - b) < 0.00001; }

// RasterVS shifts geometry by half a GPU pixel. The real GPU scissor still
// restricts samples to the first pixel; model its standard four MSAA positions.
unsigned firstPixelSamples(const ClippedTriangle& input, double scale) {
    const std::array<std::array<double, 2>, 4> samples{{{.375, .125}, {.875, .375}, {.125, .625}, {.625, .875}}};
    unsigned covered = 0;
    for (const auto& sample : samples) {
        for (uint32_t start = 0; start < input.vertexCount; start += 3) {
            bool positive = false, negative = false;
            for (unsigned edge = 0; edge < 3; edge++) {
                const auto& a = input.vertices[start + edge].position;
                const auto& b = input.vertices[start + (edge + 1) % 3].position;
                const double ax = a[0]*scale+.5, ay = a[1]*scale+.5;
                const double bx = b[0]*scale+.5, by = b[1]*scale+.5;
                const double cross = (bx-ax)*(sample[1]-ay)-(by-ay)*(sample[0]-ax);
                positive |= cross > 0.000001; negative |= cross < -0.000001;
            }
            if (!(positive && negative)) { covered++; break; }
        }
    }
    return covered;
}

// Model RasterVS's (screen NDC * W, W), then the GPU's four homogeneous
// viewport planes. This is independent of the RDP screen-space clip helper.
double gpuArea(const ClippedTriangle& input) {
    using Point = std::array<double, 3>;
    double area = 0;
    for (uint32_t start = 0; start < input.vertexCount; start += 3) {
        std::vector<Point> polygon;
        for (uint32_t i = start; i < start + 3; i++) {
            const auto& p = input.vertices[i].position;
            polygon.push_back({(2 * p[0] - 1) * p[3], (1 - 2 * p[1]) * p[3], p[3]});
        }
        for (unsigned plane = 0; plane < 4; plane++) {
            auto distance = [plane](const Point& p) { return p[2] + (plane & 1 ? -1 : 1) * p[plane / 2]; };
            std::vector<Point> next;
            for (size_t i = 0; i < polygon.size(); i++) {
                const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
                const double da = distance(a), db = distance(b);
                if (da >= 0) next.push_back(a);
                if ((da >= 0) != (db >= 0)) {
                    Point p;
                    for (unsigned c = 0; c < 3; c++) p[c] = a[c] + (b[c] - a[c]) * da / (da - db);
                    next.push_back(p);
                }
            }
            polygon = std::move(next);
        }
        for (size_t i = 1; i + 1 < polygon.size(); i++) {
            const auto& a = polygon[0]; const auto& b = polygon[i]; const auto& c = polygon[i + 1];
            if (a[2] <= 0 || b[2] <= 0 || c[2] <= 0) continue;
            area += std::abs((b[0]/b[2]-a[0]/a[2])*(c[1]/c[2]-a[1]/a[2])-
                             (b[1]/b[2]-a[1]/a[2])*(c[0]/c[2]-a[0]/a[2])) / 8;
        }
    }
    return area;
}
int main() {
    // Synthetic coordinates, no captured ROM commands: reciprocal W crosses
    // zero at y=4, outside the scissor. The visible trapezoid is 15/16 pixels.
    const std::array<TriangleVertex, 3> triangle{{
        {{{0, 0, .1f, 1}}, {{0, 0}}, {{.2f, .3f, .4f, 1}}},
        {{{1, 0, .2f, 1}}, {{1, 0}}, {{.7f, .3f, .4f, 1}}},
        {{{0, 8, .9f, -1}}, {{0, -8}}, {{-1.8f, .3f, .4f, 1}}}
    }};
    ClippedTriangle old;
    std::copy(triangle.begin(), triangle.end(), old.vertices.begin()); old.vertexCount = 3;
    require(near(gpuArea(old), 0), "Synthetic fixture no longer reproduces GPU coverage loss");
    const auto clipped = clipTriangle(triangle, {0, 0, 1, 1});
    require(near(gpuArea(clipped), 15.0/16), "Negative off-screen W still loses visible RDP coverage");
    require(clipped.vertexCount == 6, "Clipped quadrilateral was not triangulated");
    for (uint32_t i = 0; i < clipped.vertexCount; i++) {
        const auto& v = clipped.vertices[i]; const auto& p = v.position;
        require(p[0] >= 0 && p[0] <= 1 && p[1] >= 0 && p[1] <= 1 && p[3] > 0, "Output escaped scissor or positive W");
        require(near(p[3], 1/(1-p[1]/4)) && near(p[2], .1+.1*p[0]+.1*p[1]), "Position/depth gradient changed");
        require(near(v.uv[0], p[0]*p[3]) && near(v.uv[1], p[1]*p[3]), "Perspective texture plane changed");
        require(near(v.color[0], .2+(.5*p[0]+.25*p[1])*p[3]), "Perspective shade gradient changed");
        for (unsigned c = 1; c < 4; c++) require(near(v.color[c], triangle[0].color[c]), "Constant shade changed");
    }
    auto positive = triangle; positive[2].position[3] = 1;
    const auto positiveClipped = clipTriangle(positive, {0, 0, 1, 1});
    require(positiveClipped.vertexCount == 6 && near(gpuArea(positiveClipped), 15.0/16), "Positive off-screen triangle escaped RDP scissor");
    for (uint32_t i = 0; i < positiveClipped.vertexCount; i++) {
        const auto& v = positiveClipped.vertices[i];
        require(v.position[0] >= 0 && v.position[0] <= 1 && v.position[1] >= 0 && v.position[1] <= 1, "Positive output escaped scissor");
        require(near(v.uv[0], v.position[0]) && near(v.uv[1], -v.position[1]), "Positive texture gradient changed");
    }
    positive[2].position[1] = .75f;
    const auto unchanged = clipTriangle(positive, {0, 0, 1, 1});
    require(unchanged.vertexCount == 3, "Visible positive-W triangle was unnecessarily altered");
    for (unsigned i = 0; i < 3; i++) {
        require(unchanged.vertices[i].position == positive[i].position && unchanged.vertices[i].uv == positive[i].uv &&
                unchanged.vertices[i].color == positive[i].color, "Positive-W attributes drifted");
    }
    require(clipTriangle(triangle, {2, 0, 3, 1}).vertexCount == 0, "Outside triangle was retained");
    require(clipTriangle(triangle, {0, 0, 1, 8}).vertexCount == 3, "Visible singularity changed semantics");
    auto extreme = triangle;
    for (unsigned i = 0; i < 3; i++) extreme[i].uv[0] = i == 2 ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
    const auto guarded = clipTriangle(extreme, {0, 0, 1, std::nextafter(4.0f, 0.0f)});
    require(guarded.vertexCount == 3 && guarded.vertices[2].uv == extreme[2].uv, "Finite input emitted overflowing attributes");
    const std::array<TriangleVertex, 3> cancellation{{
        {{{-10000, 1e30f, .25f, 1}}, {{1, 1}}, {{1, 1, 1, 1}}},
        {{{10000, -1e30f, .25f, 1}}, {{1, 1}}, {{1, 1, 1, 1}}},
        {{{1, -1, .25f, -1}}, {{1, 1}}, {{1, 1, 1, 1}}}
    }};
    const auto bounded = clipTriangle(cancellation, {0, 0, 1, 1});
    require(bounded.vertexCount == 3 && bounded.vertices[2].position == cancellation[2].position, "Cancellation exceeded polygon capacity");
    const std::array<TriangleVertex, 3> edgeTriangle{{
        {{{-4, -4, .25f, 1}}, {{0, 0}}, {{1, 1, 1, 1}}},
        {{{8, -4, .25f, 1}}, {{0, 0}}, {{1, 1, 1, 1}}},
        {{{-4, 8, .25f, 1}}, {{0, 0}}, {{1, 1, 1, 1}}}
    }};
    ClippedTriangle edgeOriginal;
    std::copy(edgeTriangle.begin(), edgeTriangle.end(), edgeOriginal.vertices.begin()); edgeOriginal.vertexCount = 3;
    require(firstPixelSamples(clipTriangle(edgeTriangle, {0, 0, 1, 1}), 1) == 1, "Exact scissor no longer reproduces MSAA coverage loss");
    const auto edgeClipped = clipTriangle(edgeTriangle, rasterClipBounds({0, 0, 1, 1}));
    require(edgeClipped.vertexCount > 3, "MSAA fixture did not exercise clipping");
    for (double scale : {1.0, 2.0, 3.0}) {
        require(firstPixelSamples(edgeOriginal, scale) == 4, "Original fixture misses an MSAA edge sample");
        require(firstPixelSamples(edgeClipped, scale) == 4, "Clipping lost half-pixel MSAA edge coverage");
        require(firstPixelSamples(clipTriangle(edgeTriangle, rasterClipBounds({0, 0, .75f, .75f})), scale) == 4, "Fractional scissor lost MSAA edge coverage");
    }
    const auto fractional = rasterClipBounds({.25f, .75f, 1.25f, 1.75f});
    require(fractional.left == -1 && fractional.top == -1 && fractional.right == 3 && fractional.bottom == 3, "Fractional scissor guard did not cover raster rounding");
    const auto invalid = rasterClipBounds({2, 3, 1, 4});
    require(invalid.left == 2 && invalid.top == 3 && invalid.right == 1 && invalid.bottom == 4, "Invalid scissor semantics changed");
    std::puts("RDP triangle scissor/negative-W regression passed");
}
