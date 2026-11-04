#undef NDEBUG
#include "game_warp.h"
#include "recomp.h"
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

extern "C" {
void wdc_warp_menu(uint8_t*, recomp_context*);
void wdc_warp_race(uint8_t*, recomp_context*);
void wdc_warp_load(uint8_t*, recomp_context*);
void func_80068AA8(uint8_t*, recomp_context*);
void func_8003BC50(uint8_t*, recomp_context*);
extern unsigned wdc_boot_scan_calls, wdc_boot_dialog_calls;
void func_8006F63C(uint8_t*, recomp_context*) {} // ROM debug printf only.
void do_break(uint32_t) { std::abort(); }
}
int main(int argc, char** argv) {
    using namespace wdc::warp;
    assert(argc == 2);
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    std::ifstream rom(argv[1], std::ios::binary);
    assert(rom);
    rom.seekg(0x1000);
    std::vector<char> data(0x100000);
    rom.read(data.data(), data.size());
    assert(rom.gcount() == std::streamsize(data.size()));
    for (size_t i = 0; i < data.size(); ++i) MEM_B(i, int32_t(0x80000400)) = data[i];
    constexpr int32_t career = int32_t(0x80097D00), config = int32_t(0x80092360);
    // Synthetic initialized career records; metadata and constructor are real ROM.
    for (int i = 0; i < 58; ++i) MEM_W(0xE08 + i * 8, career) = i;
    for (int i = 0; i < 35; ++i) {
        MEM_W(0xB4C + i * 20 + 4, career) = 0;
        MEM_W(0xB4C + i * 20 + 8, career) = i;
    }
    auto context = [] {
        recomp_context ctx{};
        ctx.r29 = int32_t(0x807FF000);
        return ctx;
    };
    auto ctx = context();
    assert(request({1}));
    MEM_W(0, int32_t(0x8007D75C)) = int32_t(0x80180000); // Framebuffer descriptor.
    MEM_W(0, int32_t(0x80180000)) = 320;
    MEM_W(4, int32_t(0x80180000)) = 240;
    MEM_W(0, int32_t(0x8007EDC0)) = 1; // Presentation completion boundary.
    func_8003BC50(rdram, &ctx);
    assert(wdc_boot_scan_calls == 1 && wdc_boot_dialog_calls == 0);
    assert(ctx.r29 == gpr(int32_t(0x807FF000)));
    auto words = [&] {
        std::array<int32_t, 40> value;
        for (int i = 0; i < 40; ++i) value[i] = MEM_W(config, i * 4);
        return value;
    };
    unsigned cases = 0;
    for (int track = 1; track <= 58; ++track) for (int car = 0; car < 35; ++car)
        for (auto mode : {Mode::Practice, Mode::TimeAttack, Mode::Arcade}) {
            // Reference: native menu constructor, clean config and no ghost/mirror.
            for (int i = 0; i < 0xA0; i += 4) MEM_W(i, config) = 0;
            MEM_W(0x84, config) = int(mode);
            MEM_W(0x1068, career) = track - 1; MEM_W(0x106C, career) = car;
            MEM_B(0x10FC, career) = 0; MEM_B(0x1075, career) = 0;
            ctx.r4 = career;
            func_80068AA8(rdram, &ctx);
            const auto expected = words();
            // Poison every borrowed field, and require byte-exact career restoration.
            MEM_W(0x1054, career) = 123; MEM_W(0x1068, career) = 17; MEM_W(0x106C, career) = 9;
            MEM_B(0x10FC, career) = 1; MEM_B(0x1075, career) = 1;
            const std::vector<uint8_t> saved(memory.begin() + 0x97D00, memory.begin() + 0x98E04);
            ctx = context();
            const auto registers = ctx;
            MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8004E950);
            MEM_B(0, int32_t(0x80092EBC)) = car & 1; // Native attract exit also has flag 0.
            assert(request({track, car, mode}));
            wdc_warp_menu(rdram, &ctx);
            assert(MEM_BU(int32_t(0x80092EBC), 0) == 0);
            wdc_warp_load(rdram, &ctx);
            assert(words() == expected);
            assert(std::memcmp(&ctx, &registers, sizeof(ctx)) == 0);
            assert(std::memcmp(memory.data() + 0x97D00, saved.data(), saved.size()) == 0);
            const auto after = words(); wdc_warp_load(rdram, &ctx); assert(words() == after);
            ++cases;
        }
    // Override laps BEFORE native arcade profile selection, compared with a
    // native course whose default lap count has independently been set to match.
    for (int laps = 2; laps <= 5; ++laps) {
        MEM_W(0, int32_t(0x8009653C)) = laps;
        MEM_W(0x1068, career) = 0; MEM_W(0x106C, career) = 34;
        MEM_B(0x10FC, career) = 0; MEM_B(0x1075, career) = 0;
        for (int i = 0; i < 0xA0; i += 4) MEM_W(i, config) = 0;
        MEM_W(0x84, config) = 3; ctx.r4 = career;
        func_80068AA8(rdram, &ctx);
        const auto expected = words();
        MEM_W(0, int32_t(0x8009653C)) = 2;
        MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8000C560);
        MEM_W(0, int32_t(0x8007DF44)) = 0;
        assert(request({1, 34, Mode::Arcade, laps}));
        wdc_warp_race(rdram, &ctx);
        assert(MEM_W(0, int32_t(0x8007E0A8)) == int32_t(0x800369E0));
        assert(MEM_W(0, int32_t(0x8007DF44)) == 1);
        assert(MEM_W(0, int32_t(0x8007DF4C)) == 24);
        wdc_warp_load(rdram, &ctx);
        assert(words() == expected);
        for (int i = 0; i < 0xA0; i += 4)
            assert(MEM_W(config, i) == MEM_W(int32_t(0x80099B54), i));
    }
    // Repeating and requests queued while loading remain usable.
    repeat(); MEM_W(0, int32_t(0x8007DF44)) = 0;
    wdc_warp_race(rdram, &ctx);
    assert(request({58, 16})); wdc_warp_load(rdram, &ctx);
    MEM_W(0, int32_t(0x8007DF44)) = 0;
    wdc_warp_race(rdram, &ctx); wdc_warp_load(rdram, &ctx);
    assert(MEM_W(8, config) == 16 && MEM_W(0x84, config) == 8);
    std::cout << cases << " native setup comparisons, fades, laps, career restoration and queued repeats passed\n";
}
