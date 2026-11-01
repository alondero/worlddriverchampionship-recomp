#undef NDEBUG
#include "game_framerate.h"
#include "recomp.h"
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace {
bool pending = false, swapped = false, delay_landing = false;
unsigned ticks = 0, sends = 0;
uint32_t current_buffer = 0, next_buffer = 0;
void start_worker(recomp_context&);
recomp_context context() {
    recomp_context ctx{};
    ctx.r29 = int32_t(0x807FF000);
    return ctx;
}
void start(bool native, bool race = true) {
    wdc::framerate::configure(native);
    auto ctx = context();
    ctx.r4 = race ? int32_t(0x8000C0D0) : 0;
    ctx.r5 = race ? 1 : 0;
    // Exercised here as a register seam; test_startup_rom independently
    // checks the generated startup hook and its ROM stores before preemption.
    start_worker(ctx);
    assert(ctx.r5 == (race && !native ? 1 : 0));
}
}
extern "C" {
void wdc_framerate_start(uint8_t*, recomp_context*);
void func_800013DC(uint8_t*, recomp_context*);
void func_8000C0D0(uint8_t*, recomp_context*);
void func_8000C164(uint8_t*, recomp_context*);
void func_80017164(uint8_t*, recomp_context*);
void func_80017DEC(uint8_t*, recomp_context*);
void func_8001A4C4(uint8_t*, recomp_context*);
void wdc_framerate_hud_age(uint8_t*, recomp_context*);
// A controlled decaying force isolates the ROM integrators from collision,
// track and vehicle setup. It proves timestep handling, not driving fidelity.
void derivative(uint8_t* rdram, recomp_context* ctx) {
    for (int i = 0; i < 16; ++i) {
        const float value = std::bit_cast<float>(uint32_t(MEM_W(i * 4, int32_t(ctx->r5))));
        MEM_W(i * 4, int32_t(ctx->r6)) = std::bit_cast<uint32_t>(-2.0f * value);
    }
}
recomp_func_t* get_function(int32_t address) {
    assert(uint32_t(address) == 0x80018F74);
    return derivative;
}
void osSendMesg_recomp(uint8_t*, recomp_context* ctx) {
    assert(uint32_t(ctx->r4) == 0x800AAA28);
    assert(ctx->r5 == 0 && ctx->r6 == 0);
    ++sends;
    ctx->r2 = pending ? -1 : 0;
    pending = true;
    // The generated hook must isolate ALL caller-clobbered registers.
    ctx->r4 = ctx->r5 = ctx->r6 = int32_t(0xDEADBEEF);
}
void osRecvMesg_recomp(uint8_t*, recomp_context* ctx) {
    assert(uint32_t(ctx->r4) == 0x800AAA28);
    assert(ctx->r5 == 0 && ctx->r6 == 1);
    if (!pending) {
        ++ticks;
        // Mirrors events.cpp: next framebuffer becomes current before the
        // VI message. Delayed landing also exercises the ROM's extra wait.
        if (!delay_landing) current_buffer = next_buffer;
        if (swapped) delay_landing = false;
    }
    pending = false;
    ctx->r2 = 0;
}
void osViSwapBuffer_recomp(uint8_t*, recomp_context* ctx) {
    assert(!pending); // The injected message was consumed before this call.
    next_buffer = uint32_t(ctx->r4);
    swapped = true;
}
void osViGetCurrentFramebuffer_recomp(uint8_t*, recomp_context* ctx) {
    assert(swapped);
    ctx->r2 = int32_t(current_buffer);
}
void func_80019D98(uint8_t* rdram, recomp_context* ctx) {
    // Geometry does not affect the wheel-speed >=10 branch being checked.
    MEM_W(0, int32_t(ctx->r4)) = 0;
}
#define NOOP(name) void name(uint8_t*, recomp_context*) {}
NOOP(func_8002AC60) NOOP(func_80000678) NOOP(func_800216D0)
NOOP(func_80019E28) NOOP(func_80001304) NOOP(func_8002AE50)
NOOP(func_8006F63C) NOOP(func_8006F1E0) NOOP(osViBlack_recomp)
NOOP(osViSetMode_recomp) NOOP(osViSetSpecialFeatures_recomp)
NOOP(func_80013B78) NOOP(func_8001A000) NOOP(func_80013998)
#undef NOOP
}
namespace {
void start_worker(recomp_context& ctx) { wdc_framerate_start(nullptr, &ctx); }
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::vector<uint8_t> storage(8 * 1024 * 1024);
    auto* rdram = storage.data();
    std::ifstream rom(argv[1], std::ios::binary);
    rom.seekg(0x1000);
    std::vector<char> data(0x100000);
    rom.read(data.data(), data.size());
    assert(rom);
    for (size_t i = 0; i < data.size(); ++i) MEM_B(i, int32_t(0x80000400)) = data[i];

    for (bool native : {false, true}) for (bool menu : {false, true})
    for (bool queued_vi : {false, true}) for (bool delayed : {false, true}) {
        start(native, !menu);
        pending = queued_vi; swapped = false; delay_landing = delayed;
        ticks = sends = 0; current_buffer = next_buffer = 0;
        // Select the steady-state normal swap path; suppress unrelated
        // screenshot/effect callbacks and the worker's message-1 notification.
        MEM_W(0, int32_t(0x8007EDB8)) = 1;
        MEM_W(0, int32_t(0x8007D844)) = 0;
        MEM_W(0, int32_t(0x8007FE10)) = 0;
        MEM_W(0, int32_t(0x8007D764)) = 0;
        MEM_W(0, int32_t(0x8007D760)) = 0;
        MEM_W(0, int32_t(0x8007D750)) = 0x80300000;
        MEM_W(0, int32_t(0x8007D76C)) = 0;
        MEM_W(0, int32_t(0x8007D770)) = 0;
        MEM_W(0, int32_t(0x8007D768)) = 1;
        auto ctx = context();
        // A preference changed during a race must not change only one limit.
        wdc::framerate::configure(!native);
        func_800013DC(rdram, &ctx);
        assert(swapped && current_buffer == next_buffer && !pending);
        assert(sends == unsigned(native));
        assert(ticks == (native ? 1u : queued_vi ? 1u : 2u) + unsigned(delayed));
        assert(ctx.r29 == gpr(int32_t(0x807FF000)));
    }

    constexpr int32_t car = int32_t(0x800BA7F0), snapshot = int32_t(0x800B97E8);
    MEM_W(0, int32_t(0x800ABE30)) = 0;
    MEM_W(4, int32_t(0x800ABE30)) = 1;
    for (bool native : {false, true}) {
        start(native);
        MEM_W(0, snapshot) = 0x40000;
        MEM_W(0x7C, snapshot) = 0;
        MEM_W(0x2E4, car) = 0;
        auto ctx = context(); ctx.r4 = snapshot;
        func_8000C0D0(rdram, &ctx);
        const int samples = native ? 6 : 3;
        assert(MEM_W(0x2E4, car) == samples);
        for (int i = samples; i > 0; --i) {
            assert(MEM_W(0x2E4, car) == i);
            ctx = context(); func_8000C164(rdram, &ctx);
        }
        assert(MEM_W(0x2E4, car) == 0);
    }

    std::array<float, 4> stock_angles{};
    for (bool native : {false, true}) {
        start(native);
        for (int wheel = 0; wheel < 4; ++wheel) {
            MEM_W(0xBA4 + 0x30 + wheel * 4, car) = std::bit_cast<uint32_t>(12.0f + wheel);
            MEM_W(0xBA4 + 0x50 + wheel * 4, car) = 0;
        }
        for (int frame = 0; frame < (native ? 60 : 30); ++frame) {
            auto ctx = context(); ctx.r4 = car;
            func_80017164(rdram, &ctx);
        }
        for (int wheel = 0; wheel < 4; ++wheel) {
            const float angle = std::bit_cast<float>(uint32_t(MEM_W(0xBA4 + 0x50 + wheel * 4, car)));
            assert(std::isfinite(angle) && angle != 0);
            if (!native) stock_angles[wheel] = angle;
            else assert(std::abs(stock_angles[wheel] - angle) < 0.00002f);
        }
    }
    // Camera smoothing retains the stock decay after two half-steps. The
    // generic vector helper's other callers must retain their coefficients.
    constexpr int32_t target = int32_t(0x80400100), camera = target + 8;
    for (bool native : {false, true}) for (bool caller : {false, true}) {
        start(native);
        MEM_W(0, target) = std::bit_cast<uint32_t>(1.0f);
        MEM_W(4, target) = std::bit_cast<uint32_t>(-1.0f);
        MEM_W(0, camera) = MEM_W(4, camera) = 0;
        for (int sample = 0; sample < (native && caller ? 2 : 1); ++sample) {
            auto ctx = context(); ctx.r4 = camera; ctx.r5 = target; ctx.r6 = camera;
            ctx.r7 = std::bit_cast<uint32_t>(0.25f);
            ctx.r31 = int32_t(caller ? 0x800124F0 : 0x80012510);
            func_8001A4C4(rdram, &ctx);
        }
        assert(std::abs(std::bit_cast<float>(uint32_t(MEM_W(0, camera))) - 0.25f) < 0.000001f);
        assert(std::abs(std::bit_cast<float>(uint32_t(MEM_W(4, camera))) + 0.25f) < 0.000001f);
    }
    // The HUD's 32-tick hold and eight-tick flash phase agree at equal
    // elapsed times, including counter wrap (the subtraction is unsigned).
    for (unsigned initial : {0u, 0xFFFFFFF0u}) for (unsigned tick = 0; tick < 128; ++tick) {
        for (bool native : {false, true}) {
            start(native);
            auto ctx = context();
            const unsigned current = initial + tick * (native ? 2 : 1);
            ctx.r3 = uint32_t(current - initial);
            wdc_framerate_hud_age(nullptr, &ctx);
            assert((ctx.r3 > 32) == (tick > 32));
            assert((ctx.r3 & 0x18) == (tick & 0x18));
        }
    }
    // Unknown future callers retain their arguments and the original swap.
    wdc::framerate::configure(true);
    auto ctx = context(); ctx.r4 = 0x12345678; ctx.r5 = 2;
    wdc_framerate_start(nullptr, &ctx);
    assert(ctx.r4 == 0x12345678 && ctx.r5 == 2);

    // Four physics substeps per sample (record +AC8 == 0). Run the real
    // dispatcher and all three algorithms for one second at 30 and 60 Hz.
    constexpr int32_t state = int32_t(0x80400000), slope = state + 0x40, output = state + 0x80;
    for (int method : {0, 1, 2}) {
        std::array<float, 16> at_30{};
        for (int rate : {30, 60}) {
            const float step = 1.0f / (rate * 4);
            MEM_W(0, int32_t(0x8007E76C)) = method;
            MEM_W(0, int32_t(0x8007E768)) = std::bit_cast<uint32_t>(step);
            for (int i = 0; i < 16; ++i)
                MEM_W(i * 4, state) = std::bit_cast<uint32_t>(1.0f + i / 16.0f);
            for (int sample = 0; sample < rate * 4; ++sample) {
                ctx = context(); ctx.r5 = state; ctx.r6 = slope;
                derivative(rdram, &ctx);
                ctx.r4 = state; ctx.r5 = slope;
                ctx.r6 = std::bit_cast<uint32_t>(sample * step);
                ctx.r7 = std::bit_cast<uint32_t>(step);
                MEM_W(0x10, int32_t(ctx.r29)) = output;
                MEM_W(0x14, int32_t(ctx.r29)) = int32_t(0x80018F74);
                func_80017DEC(rdram, &ctx);
                for (int i = 0; i < 16; ++i) MEM_W(i * 4, state) = MEM_W(i * 4, output);
            }
            for (int i = 0; i < 16; ++i) {
                const float value = std::bit_cast<float>(uint32_t(MEM_W(i * 4, state)));
                const float expected = (1.0f + i / 16.0f) * std::exp(-2.0f);
                assert(std::isfinite(value) && std::abs(value / expected - 1.0f) < 0.02f);
                if (rate == 30) at_30[i] = value;
                else assert(std::abs(value / at_30[i] - 1.0f) < 0.01f);
            }
        }
    }
}
