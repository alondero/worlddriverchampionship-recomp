#undef NDEBUG
#include "game_camera.h"
#include "recomp.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

extern "C" void wdc_align_hud_triangle(uint8_t*, recomp_context*);

// The Hawaii needle's screen Y is doubled when the H-RES buffer is twice the
// logical picture. At 4:3 the horizontal hook returns immediately, so this
// case is the one the in-race toggle actually shows.
int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    constexpr gpr descriptor = int32_t(0x80400100);
    constexpr gpr triangle = int32_t(0x80403000);
    const int needle_y[3] = {697, 719, 838};
    const int needle_x[3] = {986, 1006, 861};
    MEM_W(0, int32_t(0x8007D75C)) = int32_t(descriptor);
    MEM_W(0, descriptor) = 320;
    MEM_W(4, descriptor) = 240;
    MEM_W(8, descriptor) = 320;
    MEM_W(12, descriptor) = 480;
    MEM_W(0, int32_t(0x8007FE84)) = 1;
    for (int i = 0; i < 3; ++i) {
        MEM_H(0x10 + i * 16, triangle) = int16_t(needle_x[i]);
        MEM_H(0x12 + i * 16, triangle) = int16_t(needle_y[i] << 1);
    }
    wdc::camera::set_output_size(960, 720);
    assert(wdc::camera::width_scale() == 1.0f);
    recomp_context ctx{};
    ctx.r2 = triangle;
    wdc_align_hud_triangle(rdram, &ctx);
    for (int i = 0; i < 3; ++i) {
        assert(MEM_H(0x12 + i * 16, triangle) == needle_y[i]);
        assert(MEM_H(0x10 + i * 16, triangle) == needle_x[i]);
    }
    // A wide window still undoes the vertical double, and still recentres X.
    wdc::camera::set_output_size(1280, 720);
    const float scale = wdc::camera::width_scale();
    assert(scale > 1.0f);
    for (int i = 0; i < 3; ++i) {
        MEM_H(0x10 + i * 16, triangle) = int16_t(needle_x[i]);
        MEM_H(0x12 + i * 16, triangle) = int16_t(needle_y[i] << 1);
    }
    wdc_align_hud_triangle(rdram, &ctx);
    for (int i = 0; i < 3; ++i) {
        const float center = 640.0f;
        const float got = float(MEM_H(0x10 + i * 16, triangle));
        const float rectangle_x = center * scale + needle_x[i] - center;
        assert(std::abs(got * scale - rectangle_x) <= 0.51f * scale);
        assert(MEM_H(0x12 + i * 16, triangle) == needle_y[i]);
    }
    // Mode 3 sets the same vertical flag, but its logical height is the
    // buffer height, so the ROM's double is the real menu coordinate.
    wdc::camera::set_output_size(960, 720);
    MEM_W(4, descriptor) = 480;
    MEM_W(12, descriptor) = 480;
    for (int i = 0; i < 3; ++i)
        MEM_H(0x12 + i * 16, triangle) = int16_t(needle_y[i] << 1);
    wdc_align_hud_triangle(rdram, &ctx);
    for (int i = 0; i < 3; ++i)
        assert(MEM_H(0x12 + i * 16, triangle) == int16_t(needle_y[i] << 1));
    // Mode 6 with the flag clear did not double Y.
    MEM_W(4, descriptor) = 240;
    MEM_W(12, descriptor) = 480;
    MEM_W(0, int32_t(0x8007FE84)) = 0;
    for (int i = 0; i < 3; ++i) {
        MEM_H(0x10 + i * 16, triangle) = int16_t(needle_x[i]);
        MEM_H(0x12 + i * 16, triangle) = int16_t(needle_y[i]);
    }
    wdc_align_hud_triangle(rdram, &ctx);
    for (int i = 0; i < 3; ++i) {
        assert(MEM_H(0x12 + i * 16, triangle) == needle_y[i]);
        assert(MEM_H(0x10 + i * 16, triangle) == needle_x[i]);
    }
    // A bad descriptor or triangle at 4:3 skips the undo. It must not abort.
    MEM_W(0, int32_t(0x8007FE84)) = 1;
    for (int i = 0; i < 3; ++i)
        MEM_H(0x12 + i * 16, triangle) = int16_t(needle_y[i] << 1);
    MEM_W(0, int32_t(0x8007D75C)) = 1;
    wdc_align_hud_triangle(rdram, &ctx);
    wdc_align_hud_triangle(nullptr, &ctx);
    MEM_W(0, int32_t(0x8007D75C)) = int32_t(descriptor);
    ctx.r2 = 0;
    wdc_align_hud_triangle(rdram, &ctx);
    ctx.r2 = triangle;
    for (int i = 0; i < 3; ++i)
        assert(MEM_H(0x12 + i * 16, triangle) == int16_t(needle_y[i] << 1));
}
