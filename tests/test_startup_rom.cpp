#undef NDEBUG
#include "recomp.h"
#include "game_framerate.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

// Exercise the actual startup and ring initializers. Model an immediate
// preemption at osStartThread: every controller record must already be ready.
namespace {
unsigned starts = 0, creates = 0;
}
extern "C" {
void func_8002CB0C(uint8_t*, recomp_context*);
void func_80003ABC(uint8_t*, recomp_context*);
void func_80004070(uint8_t*, recomp_context*);
void func_80003E74(uint8_t*, recomp_context*);
void wdc_framerate_start(uint8_t*, recomp_context*);
uint32_t wdc_framerate_preswap();
void wdc_framerate_wheels(uint8_t*, recomp_context*);
void wdc_framerate_action_lifetime(uint8_t*, recomp_context*);
void wdc_framerate_camera(uint8_t*, recomp_context*);
void wdc_framerate_hud_age(uint8_t*, recomp_context*);
void osCreateMesgQueue_recomp(uint8_t*, recomp_context*) {}
void osSendMesg_recomp(uint8_t*, recomp_context*) {}
void func_8004D79C(uint8_t*, recomp_context*) {}
void osCreateThread_recomp(uint8_t*, recomp_context* ctx) {
    ++creates;
    assert(uint32_t(ctx->r4) == 0x800B7620);
    assert(ctx->r5 == 9 && uint32_t(ctx->r6) == 0x8002C500);
}
void osStartThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ++starts;
    assert(creates == starts);
    assert(uint32_t(ctx->r4) == 0x800B7620);
    for (unsigned slot = 0; slot < 8; ++slot) {
        const int32_t base = int32_t(0x800B97E8 + slot * 0x200);
        for (unsigned port = 0; port < 4; ++port) {
            const int32_t record = base + port * 0x7C;
            assert(uint32_t(MEM_W(0x74, record)) == 0x800AAAE0 + port * 6);
            assert(MEM_W(0x78, record) == base);
        }
    }
}
}
int main() {
    std::vector<uint8_t> storage(8 * 1024 * 1024);
    auto* rdram = storage.data();
    for (unsigned run = 0; run < 4; ++run) {
        const bool native = run >= 2;
        const bool race = run % 2;
        wdc::framerate::configure(native);
        // Poison the ring on restart so stale pointers cannot hide ordering.
        std::fill(storage.begin() + 0xB97E8, storage.begin() + 0xBA7E8, 0);
        recomp_context ctx{};
        ctx.r29 = int32_t(0x807FF000);
        ctx.r16 = 0x1234;
        ctx.r17 = 0x5678;
        // Menus start the worker as (0, 0); races (0x80037B40) as
        // (func_8000C0D0, 1). a1 is the poll divider: one snapshot every
        // a1 + 1 VIs, which is what holds racing at 30 fps (issue #39).
        // The fixed step per sample is (a1 + 1) / 60 s, stored as
        // 0x8007FDF0 (high word, 0) and 0x8007FDF4 (low word).
        const uint32_t callback = race ? 0x8000C0D0 : 0;
        ctx.r4 = int32_t(callback);
        ctx.r5 = race;
        func_8002CB0C(rdram, &ctx);
        assert(uint32_t(MEM_W(0, int32_t(0x8007FE24))) == callback);
        const uint32_t divider = race && !native ? 1 : 0;
        assert(MEM_W(0, int32_t(0x8007FE1C)) == int32_t(divider));
        assert(uint32_t(MEM_W(0, int32_t(0x8007FDF0))) == 0);
        assert(uint32_t(MEM_W(4, int32_t(0x8007FDF0))) == (divider + 1) * 0x000BEBC2u);
        assert(starts == run + 1);
        assert(ctx.r29 == gpr(int32_t(0x807FF000)));
        assert(ctx.r16 == 0x1234 && ctx.r17 == 0x5678);
    }

    // Reproduce the authored demo's real replay path with synthetic two-car
    // inputs. Each field is one literal RLE token (count 0, value); the
    // action and steering decoders share a byte cursor, just as in the ROM.
    constexpr int32_t stream = int32_t(0x8007DFA8), data = int32_t(0x80400000);
    constexpr int32_t snapshot = int32_t(0x800B7400), scene = int32_t(0x800923E4);
    auto context = [] {
        recomp_context ctx{}; ctx.r29 = int32_t(0x807FF000); return ctx;
    };
    for (int mode : {0x11, 0x12, 0x13}) for (bool native : {false, true}) {
        const int bytes = 4 + 61 * 8;
        MEM_W(0, data) = bytes;
        for (int sample = 1; sample <= 61; ++sample) for (int player = 0; player < 2; ++player) {
            const int offset = 4 + (sample - 1) * 8 + player * 4;
            MEM_B(offset, data) = 0;
            MEM_B(offset + 1, data) = player ? 200 - sample : sample;
            MEM_B(offset + 2, data) = 0;
            MEM_B(offset + 3, data) = player ? 31 - sample : sample - 31;
        }
        MEM_W(0x74, stream) = MEM_W(0x78, stream) = 3;
        auto ctx = context(); ctx.r4 = stream; ctx.r5 = data; ctx.r6 = bytes;
        func_80003ABC(rdram, &ctx);
        ctx = context(); ctx.r4 = stream; func_80004070(rdram, &ctx);
        MEM_W(0, scene) = mode;
        wdc::framerate::configure(native);
        ctx = context(); ctx.r4 = int32_t(0x8000C0D0); ctx.r5 = 1;
        func_8002CB0C(rdram, &ctx);
        const unsigned period = MEM_W(0, int32_t(0x8007FE1C)) + 1;
        for (unsigned vi = 0; vi < 60; ++vi) {
            if (vi % period != 0) continue;
            ctx = context(); ctx.r4 = stream; ctx.r5 = snapshot;
            func_80003E74(rdram, &ctx);
            assert(ctx.r2 == 1);
        }
        const unsigned consumed = MEM_W(0x70, stream);
        if (consumed != 30) std::fprintf(stderr,
            "demo scene %x with native=%d consumed %u recorded inputs in 60 VIs; expected 30\n",
            mode, native, consumed);
        assert(consumed == 30);
        assert((uint32_t(MEM_W(0, snapshot)) & 0xFF0000) == (30u << 16));
        assert((uint32_t(MEM_W(0x7C, snapshot)) & 0xFF0000) == (170u << 16));
        assert(MEM_B(0x26, snapshot) == -1 && MEM_B(0x7C + 0x26, snapshot) == 1);
        const unsigned ticks = MEM_W(4, int32_t(0x8007FDF0));
        assert(std::abs(consumed * double(ticks) / 46875000.0 - 1.0) < 0.00001);
        assert(wdc_framerate_preswap() == 0);
        ctx = context(); ctx.f28.fl = 1.0f / 60.0f; ctx.r8 = 3;
        wdc_framerate_wheels(rdram, &ctx);
        wdc_framerate_action_lifetime(rdram, &ctx);
        assert(ctx.f28.fl == 1.0f / 60.0f && ctx.r8 == 3);
        ctx.r31 = int32_t(0x800124F0); ctx.r7 = std::bit_cast<uint32_t>(0.25f); ctx.r3 = 66;
        wdc_framerate_camera(rdram, &ctx);
        wdc_framerate_hud_age(rdram, &ctx);
        assert(ctx.r7 == std::bit_cast<uint32_t>(0.25f) && ctx.r3 == 66);
        assert(MEM_W(0, scene) == mode);
    }

    // A demo must not poison the user's preference for the following race,
    // nor make menus use a race divider while the old scene ID is still set.
    wdc::framerate::configure(true);
    auto ctx = context(); ctx.r4 = 0; ctx.r5 = 0;
    func_8002CB0C(rdram, &ctx);
    assert(MEM_W(0, int32_t(0x8007FE1C)) == 0 && wdc_framerate_preswap() == 1);
    MEM_W(0, scene) = 8; // Hawaii practice.
    ctx = context(); ctx.r4 = int32_t(0x8000C0D0); ctx.r5 = 1;
    func_8002CB0C(rdram, &ctx);
    assert(MEM_W(0, int32_t(0x8007FE1C)) == 0 && wdc_framerate_preswap() == 1);
    assert(MEM_W(4, int32_t(0x8007FDF0)) == 781250);
    ctx = context(); ctx.r4 = int32_t(0x8000C0D0); ctx.r5 = 1;
    wdc_framerate_start(nullptr, &ctx);
    assert(ctx.r5 == 1 && wdc_framerate_preswap() == 0);
}
