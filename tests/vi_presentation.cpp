#include "vi_presentation.h"
#include <cmath>

namespace wdc::test {
using RT64::VI;
// Only the GPU-facing value/size interfaces are replaced. All VI decoding,
// viewport, scissor and coordinate transforms come from the pinned source.
struct RenderSwapChain {
    int width, height;
    int getWidth() const { return width; }
    int getHeight() const { return height; }
};
struct RenderViewport {
    float x{}, y{}, width{}, height{};
    RenderViewport() = default;
    RenderViewport(float x, float y, float width, float height)
        : x(x), y(y), width(width), height(height) {}
};
struct RenderRect {
    RenderRect() = default;
    RenderRect(long, long, long, long) {}
};
struct VIRenderer {
    static void getViewportAndScissor(const RenderSwapChain*, const VI&, hlslpp::float2,
                                     uint32_t, bool, RenderViewport&, RenderRect&);
};

#include "vi_presentation_funcs.inc"

std::array<float, 4> vi_viewport(const VI& vi, int width, int height,
                               float scale_x, float scale_y, bool remove_borders) {
    const RenderSwapChain swapchain{width, height};
    RenderViewport viewport;
    RenderRect scissor;
    VIRenderer::getViewportAndScissor(&swapchain, vi, {scale_x, scale_y}, 1,
                                    remove_borders, viewport, scissor);
    return {viewport.x, viewport.y, viewport.width, viewport.height};
}
}
