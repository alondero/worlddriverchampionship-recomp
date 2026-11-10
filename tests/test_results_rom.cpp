#undef NDEBUG
#include "game_hud.h"
#include "recomp.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

extern "C" {
void func_80046D40(uint8_t*, recomp_context*);
void func_8002D198(uint8_t*, recomp_context*);
void wdc_results_normal_text(uint8_t*, recomp_context*);
void switch_error(const char*, uint32_t, uint32_t) { std::abort(); }
void do_break(uint32_t) { std::abort(); }
// Setup and scissor packets do not affect the sprite's coordinate arithmetic.
void func_800241C0(uint8_t*, recomp_context*) {}
void func_800245AC(uint8_t*, recomp_context*) {}
// Texture contents/TMEM loading are outside this fixture. Keep their raw-list
// link valid, using the real frame allocator, without emitting a texrect.
void func_8006E18C(uint8_t* rdram, recomp_context* ctx) {
    ctx->r4 = 2;
    func_8002D198(rdram, ctx);
    MEM_W(0, ctx->r2) = 0xDF000000;
}
// These belong to branches the results scenario must never enter.
#define UNREACHED(name) void name(uint8_t*, recomp_context*) { assert(false && #name); }
UNREACHED(func_80007FE4)
UNREACHED(func_8004C560)
UNREACHED(func_80050E24)
UNREACHED(func_80050FC4)
UNREACHED(func_800563F0)
UNREACHED(func_80057520)
UNREACHED(func_80066D54)
UNREACHED(func_80067220)
UNREACHED(func_8006C9C4)
UNREACHED(func_8006C9FC)
UNREACHED(func_8006E39C)
UNREACHED(func_8006E4E4)
UNREACHED(func_8006E740)
UNREACHED(func_8006E99C)
UNREACHED(func_8006EB8C)
UNREACHED(func_8006F63C)
#undef UNREACHED
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
    constexpr gpr descriptor = int32_t(0x80400000);
    constexpr gpr commands = int32_t(0x80500000), packets = int32_t(0x80600000);
    constexpr gpr labels = int32_t(0x800DED48), times = int32_t(0x800DEB88);
    MEM_W(0, int32_t(0x8007D75C)) = int32_t(descriptor);
    MEM_W(0, descriptor) = MEM_W(8, descriptor) = 320;
    MEM_W(0, int32_t(0x800923E4)) = 3; // Arcade Race
    MEM_W(0, int32_t(0x80092CB0)) = 3; // scrolling race results
    MEM_B(0, int32_t(0x80092C78)) = 1; // settled drawer
    // Use prebuilt text objects with the ROM's results font choices. The
    // small car/team line is set to position-only scaling by func_80050EC4
    // through func_8006CA58; driver names use func_80050E24 and scale fully.
    auto text = [&](gpr object, int height, bool position_only) {
        std::memset(rdram + (uint32_t(object) & 0x7FFFFF), 0, 0x38);
        MEM_W(8, object) = 72;
        MEM_W(12, object) = height;
        const gpr sprite = object + 0x14;
        MEM_W(4, sprite) = 72;
        MEM_W(8, sprite) = height;
        MEM_W(0x10, sprite) = 5; // no palette setup
        MEM_B(0x1C, sprite) = position_only;
    };
    for (int row = 0; row < 8; ++row) {
        text(labels + row * 0x38, 18, false);
        text(times + row * 0x38, 16, false);
        text(labels + 0x1C0 + row * 0x38, 18, false);
        text(labels + 0x380 + row * 0x38, 12, true);
    }
    text(labels + 0x578, 18, false); // title
    auto draw = [&](int logical_h, int buffer_h, int scroll) {
        MEM_W(4, descriptor) = logical_h;
        MEM_W(12, descriptor) = buffer_h;
        MEM_W(0, int32_t(0x8007E578)) = buffer_h > 256 ? 1 : 0;
        MEM_W(0, int32_t(0x800DF3E0)) = scroll;
        MEM_W(0, int32_t(0x8007ED84)) = int32_t(commands);
        MEM_W(0, int32_t(0x8007FE74)) = int32_t(packets);
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000);
        func_80046D40(rdram, &ctx);
        std::vector<wdc::hud::Texrect> out;
        for (gpr link = commands; link < MEM_W(0, int32_t(0x8007ED84)); link += 8) {
            assert(uint32_t(MEM_W(0, link)) == 0x0E000000u);
            const gpr packet = int32_t(0x80000000u | uint32_t(MEM_W(4, link)));
            if ((uint32_t(MEM_W(0, packet)) >> 24) != 0xE4) continue;
            // The BOSS E4/E1/F1 rewrite, as consumed by the production walker.
            out.push_back(wdc::hud::correct_hres_texrect({uint32_t(MEM_W(0, packet)),
                uint32_t(MEM_W(4, packet)), uint32_t(MEM_W(12, packet)),
                uint32_t(MEM_W(20, packet))}, logical_h, buffer_h));
        }
        assert(out.size() == 33); // title + four fields for each of eight rows
        return out;
    };
    bool matches = true;
    for (int scroll : {0, -5, 5, -40, 25}) {
        const auto normal = draw(240, 240, scroll);
        const auto tall = draw(240, 480, scroll);
        const auto menu = draw(480, 480, scroll);
        for (size_t i = 0; i < tall.size(); ++i) {
            // All fields retain size and spacing and scroll at the same rate.
            const int expected_top = normal[i].w1 & 0xFFF;
            const int expected_bottom = normal[i].w0 & 0xFFF;
            if (int(tall[i].w1 & 0xFFF) != expected_top ||
                int(tall[i].w0 & 0xFFF) != expected_bottom) {
                std::cerr << "scroll " << scroll << ", field " << i << ": rows "
                          << (tall[i].w1 & 0xFFF) / 4.0 << ".." << (tall[i].w0 & 0xFFF) / 4.0
                          << ", expected " << expected_top / 4.0 << ".." << expected_bottom / 4.0 << '\n';
                matches = false;
            }
            assert((tall[i].w0 & ~0xFFFu) == (normal[i].w0 & ~0xFFFu));
            assert((tall[i].w1 & ~0xFFFu) == (normal[i].w1 & ~0xFFFu));
            assert(tall[i].w3 == normal[i].w3);
            // A real 480-line menu retains the ROM's doubled origin and its
            // two font heights. No packet receives the cinematic correction.
            const bool small = i > 0 && i % 4 == 0;
            const int height = small ? 12 : (i > 0 && i % 4 == 2 ? 16 : 18);
            const int y = i == 0 ? 5 : scroll + int((i - 1) / 4) * 25 + (small ? 21 : 10);
            assert(int(menu[i].w1 & 0xFFF) == std::max(0, y * 8));
            assert(int(menu[i].w0 & 0xFFF) == std::max(0, y * 8 + height * (small ? 4 : 8)));
        }
    }
    // Descriptor failures retain the ROM's flag without touching a sprite.
    for (uint32_t pointer : {0u, 1u, 0x807FFFFCu}) {
        MEM_W(0, int32_t(0x8007D75C)) = pointer;
        recomp_context ctx{};
        ctx.r25 = 1;
        wdc_results_normal_text(rdram, &ctx);
        assert(ctx.r25 == 1);
    }
    recomp_context ctx{};
    ctx.r25 = 1;
    wdc_results_normal_text(nullptr, &ctx);
    assert(ctx.r25 == 1);
    if (matches)
        std::cout << "Results: title and eight rows match NORM through five scroll positions; 480-line menus preserved\n";
    return matches ? 0 : 1;
}
