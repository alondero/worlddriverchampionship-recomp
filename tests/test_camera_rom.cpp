#undef NDEBUG
#include "game_camera.h"
#include "game_hud.h"
#include "recomp.h"
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>

extern "C" void func_80011D54(uint8_t*, recomp_context*);
extern "C" void func_8000FC18(uint8_t*, recomp_context*);
extern "C" void func_8002D99C(uint8_t*, recomp_context*);
extern "C" void wdc_hud_begin(uint8_t*, recomp_context*);
extern "C" void wdc_hud_end(uint8_t*, recomp_context*);
extern "C" void func_8002D558(uint8_t*, recomp_context* ctx) {
    ctx->r2 = int32_t(0x80403000);
}
constexpr int32_t test_descriptor = int32_t(0x80400100);
extern "C" void func_800006AC(uint8_t*, recomp_context* ctx) { ctx->r2 = test_descriptor; }
extern "C" void func_8000125C(uint8_t* rdram, recomp_context*) {
    MEM_W(0, int32_t(0x8007D758)) = test_descriptor;
}
// Non-layout effects of scene reconfiguration are outside the rectangle seam.
extern "C" {
void func_8000E020(uint8_t*, recomp_context*) {}
void func_8000E18C(uint8_t*, recomp_context*) {}
void func_8000E1BC(uint8_t*, recomp_context*) {}
void func_8000F988(uint8_t*, recomp_context*) {}
void func_80012024(uint8_t*, recomp_context*) {}
void func_80012218(uint8_t*, recomp_context*) {}
void func_800122F4(uint8_t*, recomp_context*) {}
void func_80019EC4(uint8_t*, recomp_context*) {}
void func_8002A334(uint8_t*, recomp_context*) {}
}
// The allocator does not affect camera math; capture its real float matrix.
std::array<float, 16> matrix;
extern "C" void func_8002CF30(uint8_t* rdram, recomp_context* ctx) {
    for (int i = 0; i < 16; ++i) matrix[i] = std::bit_cast<float>(uint32_t(MEM_W(i * 4, ctx->r4)));
    ctx->r2 = int32_t(0x80402000);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    std::ifstream rom(argv[1], std::ios::binary);
    assert(rom);
    rom.seekg(0x1000);
    std::vector<char> data(0x100000);
    rom.read(data.data(), data.size());
    assert(rom);
    for (size_t i = 0; i < data.size(); ++i) MEM_B(i, int32_t(0x80000400)) = data[i];
    constexpr gpr camera = int32_t(0x80400000), descriptor = camera + 0x100;
    auto put = [&](gpr address, int offset, float value) { MEM_W(offset, address) = std::bit_cast<uint32_t>(value); };
    // Identity view, a 60-degree vertical FOV and its tangent for CPU culling.
    put(camera, 0x10, -1); put(camera, 0x24, 1); put(camera, 8, -1);
    put(camera, 0x68, 1.04719755f); put(camera, 0x74, std::tan(1.04719755f));
    MEM_W(0, int32_t(0x8007D75C)) = int32_t(descriptor);
    auto project = [&](int window_width, int window_height, float viewport_height, float pixel_aspect) {
        wdc::camera::set_output_size(window_width, window_height);
        put(descriptor, 0x14, pixel_aspect);
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000); ctx.r4 = camera;
        ctx.r5 = std::bit_cast<uint32_t>(320.0f);
        ctx.r6 = std::bit_cast<uint32_t>(viewport_height);
        ctx.r7 = std::bit_cast<uint32_t>(100.0f);
        func_80011D54(rdram, &ctx);
        assert(MEM_W(0, int32_t(0x8007ED68)) == int32_t(0x80402000));
        return std::bit_cast<float>(uint32_t(MEM_W(0x7C, camera)));
    };
    const float original_extent = project(960, 720, 240, 1);
    const auto original = matrix;
    const float wide_extent = project(1920, 1080, 240, 1);
    // The horizontal FOV grows while vertical projection/depth stay identical.
    assert(std::abs(wide_extent / original_extent - 4.0f / 3.0f) < 1e-5f);
    assert(std::abs(matrix[0] / original[0] - 0.75f) < 1e-5f);
    for (int i = 1; i < 16; ++i) assert(std::abs(matrix[i] - original[i]) < 1e-5f);
    // A point outside the original horizontal clip plane enters the wide view.
    const float x = 1.1f / original[0];
    assert(x * original[0] > 1 && x * matrix[0] < 1);
    const auto wide = matrix;
    project(1920, 1080, 480, 2); // non-square-pixel menus use the same TV aspect
    for (int i = 0; i < 16; ++i) assert(std::abs(matrix[i] - wide[i]) < 1e-5f);
    project(2560, 1080, 240, 1);
    assert(std::abs(matrix[0] / original[0] - 0.5625f) < 1e-5f);
    project(720, 960, 240, 1);
    assert(std::abs(matrix[0] - original[0]) < 1e-5f);
    // Invalid output must reset a previously wide camera, independently of
    // the narrow-window case above. Check the real projection and cull extent.
    for (const auto dimensions : {std::array{0, 0}, std::array{0, 1080},
                                 std::array{1920, 0}, std::array{-1, 1080},
                                 std::array{1920, -1}, std::array{-1, -1}}) {
        project(2560, 1080, 240, 1);
        assert(wdc::camera::width_scale() > 1);
        const float extent = project(dimensions[0], dimensions[1], 240, 1);
        assert(wdc::camera::width_scale() == 1);
        assert(std::abs(extent - original_extent) < 1e-5f);
        for (int i = 0; i < 16; ++i) assert(std::abs(matrix[i] - original[i]) < 1e-5f);
    }
    // Exercise the ROM's scene layout, including half-screen partitions.
    constexpr int32_t scene = int32_t(0x800ABE40);
    for (int players : {0, 1, 2}) {
        MEM_W(0, descriptor) = 320; MEM_W(4, descriptor) = 240;
        MEM_W(8, descriptor) = 320; MEM_W(12, descriptor) = 240;
        MEM_W(0, int32_t(0x8007D758)) = int32_t(descriptor);
        MEM_W(0, int32_t(0x8007E57C)) = players == 1 ? 0 : 1;
        MEM_W(0, int32_t(0x8007E578)) = -1; // force reconfiguration
        MEM_W(0, int32_t(0x8007E584)) = players == 0 ? 0 : 2;
        recomp_context ctx{}; ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000);
        func_8000FC18(rdram, &ctx);
        assert(MEM_W(0, scene) == 0 && MEM_W(4, scene) == 0);
        if (players == 0) {
            assert(MEM_W(8, scene) == 320 && MEM_W(12, scene) == 240);
        } else if (players == 1) {
            assert(MEM_W(8, scene) == 320 && MEM_W(12, scene) == 119);
            assert(MEM_W(4, scene + 0x2830) == 120);
        } else {
            assert(MEM_W(8, scene) == 159 && MEM_W(12, scene) == 240);
            assert(MEM_W(0, scene + 0x2830) == 160);
        }
    }
    // The gauge uses rectangle artwork but a BOSS screen-space triangle for
    // its needle. Exercise the real builder with the stationary Hawaii needle
    // captured in a live run, plus the ROM's doubled horizontal coordinate mode.
    constexpr gpr points = int32_t(0x80404000), triangle = int32_t(0x80403000);
    const std::array<int, 3> needle_x{986, 1006, 861};
    const std::array<int, 3> needle_y{697, 719, 838};
    for (int shift : {0, 1}) {
        MEM_W(8, descriptor) = 320 << shift;
        MEM_W(0, int32_t(0x8007FE80)) = shift;
        MEM_W(0, int32_t(0x8007FE84)) = 0;
        for (int i = 0; i < 3; ++i) {
            MEM_W(i * 16, points) = needle_x[i];
            MEM_W(i * 16 + 4, points) = needle_y[i];
            MEM_W(i * 16 + 8, points) = 192 + i * 256;
            MEM_W(i * 16 + 12, points) = -128;
        }
        for (int placement : {0, 1, 2}) for (int width : {720, 960, 1280, 2560}) {
            wdc::hud::set_placement(placement);
            wdc::camera::set_output_size(width, 720);
            const float scale = wdc::camera::width_scale();
            MEM_W(0, int32_t(0x8007ED84)) = int32_t(0x80405000);
            MEM_W(0, int32_t(0x8007FE74)) = int32_t(0x80408000);
            MEM_W(0, int32_t(0x8007E570)) = scene;
            MEM_W(0, scene) = 0; MEM_W(8, scene) = 320 << shift;
            recomp_context ctx{}; ctx.f_odd = &ctx.f0.u32h;
            ctx.r29 = int32_t(0x807FF000); ctx.r4 = int32_t(0x80080578);
            ctx.r5 = points; ctx.r6 = points + 16; ctx.r7 = points + 32;
            wdc_hud_begin(rdram, &ctx);
            assert(uint32_t(MEM_W(0, int32_t(0x80407FC0))) == wdc::hud::marker);
            assert(MEM_W(4, int32_t(0x80407FC0)) == (320 << shift));
            func_8002D99C(rdram, &ctx);
            wdc_hud_end(rdram, &ctx);
            assert(uint32_t(MEM_W(0, int32_t(0x80407F80))) == wdc::hud::marker);
            assert(uint32_t(MEM_W(4, int32_t(0x80407F80))) == wdc::hud::end_marker);
            const auto closed_cursor = MEM_W(0, int32_t(0x8007ED84));
            wdc_hud_end(rdram, &ctx); // Merge-label-only path must not emit a second close.
            assert(MEM_W(0, int32_t(0x8007ED84)) == closed_cursor);
            for (int i = 0; i < 3; ++i) {
                const int x = MEM_H(0x10 + i * 16, triangle);
                const float center = float(640 << shift);
                // After RT64 stretches raw triangles, their x must match the
                // centred, unstretched rectangle path (within half a subpixel).
                const float travel = placement == 0 ? 0 :
                    (placement == 1 ? std::min(scale, 4.0f / 3.0f) : scale) - 1;
                const float rectangle_x = center * scale + (needle_x[i] << shift) - center + center * travel;
                assert(std::abs(x * scale - rectangle_x) <= 0.51f * scale);
                assert(MEM_H(0x12 + i * 16, triangle) == needle_y[i]);
                assert(MEM_H(0x18 + i * 16, triangle) == 192 + i * 256);
                assert(MEM_H(0x1A + i * 16, triangle) == -128);
                assert(MEM_W(i * 16, points) == needle_x[i]);
            }
        }
    }
    std::cout << "PASS: ROM camera, layout and gauge triangle preserve proportions\n";
}
