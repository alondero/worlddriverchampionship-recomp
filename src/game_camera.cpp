#include "game_camera.h"
#include "recomp.h"
#include <algorithm>
#include <atomic>

namespace wdc::camera {
namespace { std::atomic<float> scale{1.0f}; }
void set_output_size(int width, int height) {
    if (width > 0 && height > 0)
        scale = std::max(1.0f, float(width) / float(height) / (4.0f / 3.0f));
}
float width_scale() { return scale.load(); }
}

// USA func_80011D54, before 0x80011DC4: f0 is the float32 viewport
// width/height times the display descriptor's pixel-aspect correction.
// This game-thread register hook runs before the ROM stores camera+0x7C's
// horizontal culling extent and passes the aspect to guPerspectiveF. It
// retains the vertical FOV, viewport and guest VI/simulation timing. No guest
// pointer is read here. RDRAM remains word-swapped and owned by the runtime.
// Remove when the guest builds a widescreen camera itself; see renderer docs.
extern "C" void wdc_widen_camera(uint8_t*, recomp_context* ctx) {
    ctx->f0.fl *= wdc::camera::width_scale();
}

// USA func_8000FC18, before 0x8000FEF0: s4/s5 hold the 8- or 16-pixel
// horizontal/vertical TV overscan margins, in native framebuffer pixels.
// Set them to zero before the ROM computes scene rectangles, projection,
// viewport and scissor. Internal split-screen gaps and cinematic layouts
// remain owned by the ROM. This game-thread register hook reads no pointer
// and does not change framebuffer allocations or VI timing.
extern "C" void wdc_remove_overscan(uint8_t*, recomp_context* ctx) {
    ctx->r20 = 0;
    ctx->r21 = 0;
}
