#include "game_framerate.h"
#include "recomp.h"
#include <atomic>
#include <bit>
#include <cmath>

namespace {
std::atomic<bool> requested{};
std::atomic<bool> racing_60{};
bool skip_preswap = false; // Game thread only, like input-worker startup.
}

void wdc::framerate::configure(bool native_60fps) {
    requested.store(native_60fps, std::memory_order_relaxed);
}

// USA func_8002CB0C entry, game thread, before any worker or ring is started.
// a0 is the guest callback address (u32), a1 is the VI poll divider (s32).
// Only the two measured callers are accepted: menus (0,0), races
// (8000C0D0,1). All other argument pairs retain the ROM's behavior. No guest
// RAM is accessed here. The ROM owns and writes 8007FE24 (callback, u32),
// 8007FE1C (divider, s32), and 8007FDF0/4 (u64 ticks, big-endian high/low
// words) before osStartThread. Divider 0 means 781250 ticks = 1/60 second;
// divider 1 means 1562500 ticks = 1/30 second. Changing the argument, rather
// than the stored divider, keeps the fixed step and polling inseparable.
// The latch survives Apply/Discard until the next worker startup; neither
// queued samples nor a running worker's countdown are rewritten.
// Replace with a named input-worker API after decompilation.
extern "C" void wdc_framerate_start(uint8_t*, recomp_context* ctx) {
    const bool menu = uint32_t(ctx->r4) == 0 && ctx->r5 == 0;
    const bool race = uint32_t(ctx->r4) == 0x8000C0D0 && ctx->r5 == 1;
    skip_preswap = requested.load(std::memory_order_relaxed) && (menu || race);
    racing_60.store(skip_preswap && race, std::memory_order_relaxed);
    if (skip_preswap && race) ctx->r5 = 0;
}

// USA func_800013DC before 8000159C, game thread. The generated hook posts
// message 0 NONBLOCKING to game-owned VI queue 800AAA28 (OSMesgQueue, 24
// bytes; one u32 message slot at 8007D740). Addresses are fixed within 8 MiB
// RDRAM; native osSendMesg handles N64 big-endian/word-swapped fields and
// queue synchronization. A full queue is left intact. The immediately
// following original receive consumes either that VI or our message.
// No message is left to satisfy the post-swap wait. osViSwapBuffer still
// queues a next-VI update; 800015BC and the current-framebuffer check remain
// stock. Context is copied so native send cannot clobber guest registers.
// Off is a strict no-op. Replace with a named presentation API when recovered.
extern "C" uint32_t wdc_framerate_preswap() {
    return skip_preswap;
}

// USA func_80017164 after the ROM's f28 load (800171B8). This is wheel
// animation, not the integrator: wheel speed * 1/60 advances each angle at
// +50 of the four wheel slots. Preserve the stock angle per second at twice
// the sample rate. Register only; f28 is saved/restored by the ROM.
extern "C" void wdc_framerate_wheels(uint8_t*, recomp_context* ctx) {
    if (racing_60.load(std::memory_order_relaxed)) ctx->f28.fl *= 0.5f;
}

// USA input-worker callback before 8000C140. t0 is the three-sample lifetime
// written by the ROM for action bit 0x40000. func_8000C164 decrements it once
// per consumed sample; six samples at 60 Hz retain its stock 0.1 seconds.
// Register only; the callback still owns its player mapping.
extern "C" void wdc_framerate_action_lifetime(uint8_t*, recomp_context* ctx) {
    if (racing_60.load(std::memory_order_relaxed)) ctx->r8 = 6;
}

// USA func_8001A4C4 entry, game thread. Only the camera's call at 800124E8
// uses this correction: a3 is float32 0.25, a per-sample smoothing fraction.
// Two half-steps retain the original exponential decay: 1-sqrt(1-0.25).
// Caller identity is the MIPS return address (800124F0); the generator emits
// real JAL links at every call to this helper. Other callers and coefficients
// remain stock. Register only; source/destination vectors stay ROM-owned.
extern "C" void wdc_framerate_camera(uint8_t*, recomp_context* ctx) {
    if (racing_60.load(std::memory_order_relaxed) && uint32_t(ctx->r31) == 0x800124F0 &&
        uint32_t(ctx->r7) == std::bit_cast<uint32_t>(0.25f))
        ctx->r7 = std::bit_cast<uint32_t>(1.0f - std::sqrt(0.75f));
}

// USA func_8002E13C before 8002E24C, game thread. v1 is the unsigned age
// in consumed samples, already computed from 8007E530 minus the HUD object's
// saved counter. Its following >32 delay and 0x18 flash mask assume 30 Hz.
// Convert only this reader to stock ticks; the underlying counter and object
// remain ROM-owned, including unsigned wrap. No guest RAM is accessed.
extern "C" void wdc_framerate_hud_age(uint8_t*, recomp_context* ctx) {
    if (racing_60.load(std::memory_order_relaxed)) ctx->r3 = uint32_t(ctx->r3) / 2;
}
