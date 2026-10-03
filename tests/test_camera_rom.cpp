#undef NDEBUG
#include "game_camera.h"
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
    std::cout << "PASS: ROM projection and horizontal culling widen without changing vertical FOV\n";
}
