#include "game_camera.h"
#include "game_hud.h"
#include "recomp.h"
#include "guest_memory.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace wdc::camera {
namespace { std::atomic<float> scale{1.0f}; }
void set_output_size(int width, int height) {
    if (width > 0 && height > 0)
        scale = std::max(1.0f, float(width) / float(height) / (4.0f / 3.0f));
    else
        scale = 1.0f;
}
float width_scale() { return scale.load(); }
}

// USA func_80011D54, before 0x80011DC4: f8 is float32 viewport width/height;
// t6 points at the active ROM-owned display descriptor (loaded from 8007D75C).
// Its s32 +0/+4 are the full picture's logical width/height in pixels, not
// +8/+12's buffer dimensions. Read 8 aligned bytes of runtime-owned,
// word-swapped RDRAM on the game thread, before display-list submission;
// retain no pointer. Invalid ranges/dimensions fail before any guest write.
// RT64 presents the sampled picture across the output, including cinematic
// modes whose ROM pixel correction assumes a letterboxed VI instead. Map the
// viewport's fraction of the logical picture to the output's physical aspect.
// f0 still feeds both camera+0x7C's cull extent and guPerspectiveF; vertical FOV,
// viewport partitions, buffer allocations and VI/simulation timing stay intact.
// Remove when the guest camera owns output aspect; see renderer docs.
extern "C" void wdc_widen_camera(uint8_t* rdram, recomp_context* ctx) {
    const gpr descriptor = ctx->r14;
    wdc_require_guest_range(descriptor, 8, 4);
    const int32_t width = MEM_W(0, descriptor), height = MEM_W(4, descriptor);
    if (width <= 0 || width > 4096 || height <= 0 || height > 4096) {
        std::fprintf(stderr, "[wdc] invalid camera display size: %dx%d\n", width, height);
        std::abort();
    }
    ctx->f0.fl = ctx->f8.fl * (float(height) / float(width)) *
        (4.0f / 3.0f) * wdc::camera::width_scale();
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

// USA func_8002D99C, before 0x8002DAA4: v0 owns a newly built 0x40-byte
// BOSS HUD triangle. Its three x coordinates are signed 16-bit quarter-pixel
// values at +0x10/+0x20/+0x30. Both callers are the gauge needle layouts in
// func_8002E13C. Unlike the dial's RDP rectangles, raw triangles receive RT64's
// horizontal stretch, so undo it about the framebuffer centre before submit,
// adding the same viewport-edge travel as the pinned dial first.
// Game-thread only, before linking the packet; runtime-owned word-swapped
// RDRAM, no retained pointers. The u32 descriptor pointer at 0x8007D75C and its
// u32 framebuffer width (+8, pixels) are ROM-owned, updated on display changes.
// Invalid pointers/widths abort before any write. Source vertices, y and UVs
// stay untouched. Remove when the guest HUD or renderer carries aspect policy.
extern "C" void wdc_align_hud_triangle(uint8_t* rdram, recomp_context* ctx) {
    const float scale = wdc::camera::width_scale();
    if (scale == 1.0f) return;
    const gpr descriptor = MEM_W(0, int32_t(0x8007D75C));
    wdc_require_guest_range(descriptor, 12, 4);
    wdc_require_guest_range(ctx->r2, 0x40, 4);
    const uint32_t width = uint32_t(MEM_W(8, descriptor));
    if (width == 0 || width > 4096) {
        std::fprintf(stderr, "[wdc] invalid HUD framebuffer width: %u\n", width);
        std::abort();
    }
    const float center = float(width) * 2.0f;
    for (int offset : {0x10, 0x20, 0x30}) {
        const float x = float(MEM_H(offset, ctx->r2));
        const float shift = 4.0f * wdc::hud::needle_shift(x / 4.0f, float(width), scale);
        MEM_H(offset, ctx->r2) = int16_t(std::lround(center + (x - center + shift) / scale));
    }
}
