#undef NDEBUG
#include "game_camera.h"
#include "recomp.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

extern "C" void wdc_align_hud_triangle(uint8_t*, recomp_context*);
extern "C" void wdc_hud_normal_layout(uint8_t*, recomp_context*);
extern "C" void wdc_hud_normal_shift(uint8_t*, recomp_context*);

// H-RES keeps a 240-line picture in a buffer twice that tall. func_8002E13C
// would switch to its second HUD layout there: stack flag +0x1F0, Y shift
// 0x8007FE84 and, in mode 6, the scene's H-RES configuration (small dial) in
// a1. The hooks keep it on the normal layout, so the needle builder never sees
// a doubled Y and its hook only corrects X.
int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    constexpr gpr descriptor = int32_t(0x80400100);
    constexpr gpr triangle = int32_t(0x80403000);
    constexpr gpr stack = int32_t(0x807FF000);
    constexpr gpr config = int32_t(0x80400200);  // scene+0x2C4 copy
    constexpr gpr one_player = int32_t(0x8007E5BC);  // layout-0 entries
    constexpr gpr two_player = int32_t(0x8007E5D4);
    const int needle_y[3] = {697, 719, 838};
    const int needle_x[3] = {986, 1006, 861};
    auto picture = [&](int logical_h, int buffer_h) {
        MEM_W(0, int32_t(0x8007D75C)) = int32_t(descriptor);
        MEM_W(0, descriptor) = 320;
        MEM_W(4, descriptor) = logical_h;
        MEM_W(8, descriptor) = 320;
        MEM_W(12, descriptor) = buffer_h;
    };
    // What the ROM leaves before each hook in mode 6: the tall-buffer flag
    // stored at 0x8002E1A4, and a Y shift of 1 from func_8004D120 or the
    // scene copy at 0x8002E52C.
    auto tall_state = [&] {
        MEM_W(0x1F0, stack) = 1;
        MEM_W(0, int32_t(0x8007FE84)) = 1;
    };
    recomp_context ctx{};
    ctx.r29 = stack;
    ctx.r5 = config;

    // Modes 6 and 8: both hooks clear the flag and Y shift. Mode 6 (variant
    // 0) also takes the normal race's configuration for its player count;
    // mode 8's own entry (variant 1) is kept.
    for (int variant : {0, 1})
        for (int players : {1, 2}) {
            picture(240, 480);
            MEM_W(8, descriptor) = variant ? 640 : 320;
            MEM_W(0, int32_t(0x8007E5B4)) = variant;
            MEM_W(0, int32_t(0x8007E580)) = players;
            tall_state();
            wdc_hud_normal_layout(rdram, &ctx);
            assert(MEM_W(0x1F0, stack) == 0);
            assert(MEM_W(0, int32_t(0x8007FE84)) == 0);
            MEM_W(0, int32_t(0x8007FE84)) = 1;
            ctx.r5 = config;
            wdc_hud_normal_shift(rdram, &ctx);
            assert(MEM_W(0, int32_t(0x8007FE84)) == 0);
            assert(ctx.r5 == (variant ? config : players == 2 ? two_player : one_player));
        }
    MEM_W(0, int32_t(0x8007E5B4)) = 0;
    MEM_W(0, int32_t(0x8007E580)) = 1;
    // Mode 3 is a real 480-line menu and mode 1 a normal race: unchanged.
    // A bad descriptor pointer leaves the ROM's own choice.
    for (int bad = 0; bad < 3; ++bad) {
        if (bad == 0) picture(480, 480);
        if (bad == 1) picture(240, 240);
        if (bad == 2) { picture(240, 480); MEM_W(0, int32_t(0x8007D75C)) = 1; }
        tall_state();
        ctx.r5 = config;
        wdc_hud_normal_layout(rdram, &ctx);
        wdc_hud_normal_shift(rdram, &ctx);
        assert(MEM_W(0x1F0, stack) == 1);
        assert(MEM_W(0, int32_t(0x8007FE84)) == 1);
        assert(ctx.r5 == config);
    }
    wdc_hud_normal_layout(nullptr, &ctx);
    wdc_hud_normal_shift(nullptr, &ctx);

    // The needle hook no longer touches Y, at 4:3 or wide, even if a flag is
    // set: halving about row 0 put the needle in the top half of the screen.
    picture(240, 480);
    MEM_W(0, int32_t(0x8007FE84)) = 1;
    for (int i = 0; i < 3; ++i) {
        MEM_H(0x10 + i * 16, triangle) = int16_t(needle_x[i]);
        MEM_H(0x12 + i * 16, triangle) = int16_t(needle_y[i]);
    }
    wdc::camera::set_output_size(960, 720);
    assert(wdc::camera::width_scale() == 1.0f);
    ctx.r2 = triangle;
    wdc_align_hud_triangle(rdram, &ctx);
    for (int i = 0; i < 3; ++i) {
        assert(MEM_H(0x12 + i * 16, triangle) == needle_y[i]);
        assert(MEM_H(0x10 + i * 16, triangle) == needle_x[i]);
    }
    wdc::camera::set_output_size(1280, 720);
    const float scale = wdc::camera::width_scale();
    assert(scale > 1.0f);
    wdc_align_hud_triangle(rdram, &ctx);
    for (int i = 0; i < 3; ++i) {
        const float center = 640.0f;
        const float got = float(MEM_H(0x10 + i * 16, triangle));
        const float rectangle_x = center * scale + needle_x[i] - center;
        assert(std::abs(got * scale - rectangle_x) <= 0.51f * scale);
        assert(MEM_H(0x12 + i * 16, triangle) == needle_y[i]);
    }
}
