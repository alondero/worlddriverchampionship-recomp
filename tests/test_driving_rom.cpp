#undef NDEBUG
#include "game_driving.h"
#include "recomp.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
void func_80002F58(uint8_t*, recomp_context*);
void func_80016DF8(uint8_t*, recomp_context*);
}
int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
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
    constexpr int32_t record = int32_t(0x800B97E8), pad = int32_t(0x800AAAE0);
    constexpr int32_t car = int32_t(0x800BA7F0), drive = car + 0xBA4;
    std::fill(memory.begin() + 0xB97E8, memory.begin() + 0xBA7E8, 0);
    MEM_W(0, int32_t(0x8007DF94)) = 0;
    MEM_W(0, int32_t(0x80092350)) = 0; // Debug power multiplier disabled.
    MEM_W(0, int32_t(0x800923E4)) = 0; // Multiplayer catch-up disabled.
    auto put = [&](int offset, float value) { MEM_W(offset, drive) = std::bit_cast<uint32_t>(value); };
    auto get = [&](int offset) { return std::bit_cast<float>(uint32_t(MEM_W(offset, drive))); };
    auto context = [] {
        recomp_context ctx{};
        ctx.r29 = int32_t(0x807FF000);
        return ctx;
    };
    auto input = [&](float throttle, float brake, uint16_t buttons = 0, int y = 0) {
        wdc::driving::publish(0, {throttle, brake});
        MEM_W(0x74, record) = pad;
        MEM_H(0, pad) = buttons;
        MEM_B(2, pad) = 0; MEM_B(3, pad) = y; MEM_B(4, pad) = 0;
        auto ctx = context(); ctx.f_odd = &ctx.f0.u32h;
        ctx.r4 = record;
        func_80002F58(rdram, &ctx);
        return uint32_t(MEM_W(0, record));
    };
    // Real raw-button translation and action construction, preserving menu bits.
    assert(input(0.5f, 0.5f) == 0x60050000);
    assert(input(0.5f, 0.5f, 0xC000) == 0x00050030);
    assert(input(0.5f, 0, 0, -40) == 0x20010000);
    assert(input(0.5f, 0, 0, -41) == 0x20030000);
    assert(input(0, 0) == 0);
    MEM_W(0, int32_t(0x8007DF94)) = 2;
    assert(input(1, 1) == 0); // Replay ignores live pedals.
    MEM_W(0, int32_t(0x8007DF94)) = 0;
    MEM_B(0x27, record) = 1;
    assert(input(1, 1) == 0); // Original driving-disabled gate.
    MEM_B(0x27, record) = 0;

    auto simulate = [&](float throttle, float brake, uint16_t buttons = 0, int y = 0) {
        std::fill(memory.begin() + uint32_t(drive - int32_t(0x80000000)),
                  memory.begin() + uint32_t(drive - int32_t(0x80000000)) + 0xD00, 0);
        // Controlled car parameters, with real ROM constants and all translated
        // gearbox, steering, torque-curve and brake decisions left in the closure.
        MEM_W(0x240, drive) = y < -40 ? 0 : 1;
        MEM_B(0xCF8, drive) = 1; // Manual gears keep this fixture's selected ratio.
        put(0x23C, 5); put(0x248, 2); put(0x24C, 2); put(0x264, 1);
        put(0x38, 10); put(0x3C, 10);
        put(0x280, 500); put(0xCF4, 1000);
        put(0x34C, 2); put(0x350, 2);
        put(0x35C, 100); put(0x360, 100); put(0x364, 80); put(0x368, 80);
        input(throttle, brake, buttons, y);
        auto ctx = context(); ctx.f_odd = &ctx.f0.u32h;
        ctx.r5 = record; ctx.r6 = car;
        func_80016DF8(rdram, &ctx);
        assert(ctx.r29 == gpr(int32_t(0x807FF000)));
        const std::array result{get(0x21C), get(0x220), get(0x204), get(0x208), get(0x20C), get(0x210)};
        for (float v : result) assert(std::isfinite(v));
        return result;
    };
    auto close = [](float a, float b) { assert(std::abs(a - b) < 0.01f); };
    const auto full = simulate(0, 0, 0x8000);
    assert(full[0] > 0 && full[0] == full[1]);
    for (float value : {0.25f, 0.5f, 1.0f}) {
        const auto analog = simulate(value, 0);
        close(analog[0], full[0] * value); close(analog[1], full[1] * value);
        close(simulate(value, 0, 0x8000)[0], full[0]);
    }
    const auto coast = simulate(0, 0);
    assert(coast[0] == 0 && coast[1] == 0);
    const auto stop = simulate(0, 0, 0x4000);
    assert(stop[2] > coast[2] && stop[4] > 0);
    for (float value : {0.25f, 0.5f, 1.0f}) {
        const auto analog = simulate(0, value);
        close(analog[2], coast[2] + value * (stop[2] - coast[2]));
        close(analog[3], analog[2]); close(analog[4], value * stop[4]); close(analog[5], analog[4]);
        close(simulate(0, value, 0x4000)[2], stop[2]);
    }
    const auto reverse = simulate(1, 0, 0, -60);
    assert(reverse[0] < 0);
    close(simulate(0.5f, 0, 0, -60)[0], reverse[0] * 0.5f);
    const auto both = simulate(0.5f, 0.5f);
    close(both[0], full[0] * 0.5f);
    close(both[2], coast[2] + 0.5f * (stop[2] - coast[2]));
    std::cout << "translated driving input and torque tests passed\n";
}
