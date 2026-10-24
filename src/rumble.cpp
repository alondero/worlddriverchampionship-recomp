#include "rumble.h"
#include "recomp.h"
#include <atomic>
#include <chrono>

namespace {
std::atomic<uint64_t> motor_until{};
std::atomic<bool> initialized{};
constexpr uint32_t MotorPfs = 0x800AAC98;
constexpr uint64_t LeaseMs = 150;
uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

void wdc::rumble::set_motor(int port, bool on) {
    if (port == 0) motor_until.store(on ? now_ms() + LeaseMs : 0, std::memory_order_relaxed);
}
bool wdc::rumble::active() {
    return now_ms() < motor_until.load(std::memory_order_relaxed);
}
void wdc::rumble::stop() { set_motor(0, false); }

// USA motor adapter, synchronous on the calling guest thread. No guest memory
// is accessed or retained: a1 (Init) / a0 (Start/Stop) is an identity token for
// the game's motor-only OSPfs at 800AAC98 (104 bytes, owned by the input worker).
// Channel a2 is zero-based. The separate filesystem OSPfs at 800AAAF8 must fail
// motor detection with PFS_ERR_DEVICE (11), preserving the Controller Pak UI.
// Other pointers fail without dereferencing; absent channels return CONTRFAIL
// (4), and an uninitialized motor returns INVALID (5). Physical pad support is
// deliberately independent of this virtual device and cannot disable saves.
// Replace this address identity with a named accessory API when decompiled.
extern "C" void wdc_motor_init(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 4;
    if (ctx->r6 != 0) return;
    ctx->r2 = 11;
    if (static_cast<uint32_t>(ctx->r5) != MotorPfs) return;
    initialized.store(true, std::memory_order_relaxed);
    wdc::rumble::stop();
    ctx->r2 = 0;
}
extern "C" void wdc_motor_start(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 5;
    if (static_cast<uint32_t>(ctx->r4) != MotorPfs || !initialized.load(std::memory_order_relaxed)) return;
    wdc::rumble::set_motor(0, true);
    ctx->r2 = 0;
}
extern "C" void wdc_motor_stop(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 5;
    if (static_cast<uint32_t>(ctx->r4) != MotorPfs || !initialized.load(std::memory_order_relaxed)) return;
    wdc::rumble::stop();
    ctx->r2 = 0;
}

// Before 8004D760 in func_8004D450, s1 is the zero-based player. This path is
// reached only while the ROM permits effects (outside pause/replay/menu gates).
// Refresh held motor commands between the ROM's timed start/stop edges. CAS
// prevents a concurrent SDL-thread mute from being undone by this refresh.
// Read two game-owned u16 values at 800DF464 (effect pending) and 800DF468
// (motor on), using word-swapped big-endian helpers on the input worker thread.
// Both fixed addresses are inside the runtime's 8 MiB RDRAM; no pointers are
// retained. A reset/finished effect cancels the lease. Replace these fields with
// a named effect API when decompiled. Port 2 remains absent.
extern "C" void wdc_rumble_tick(uint8_t* rdram, recomp_context* ctx) {
    if (ctx->r17 != 0) return;
    if (MEM_H(0, static_cast<int32_t>(0x800DF464)) == 0 ||
        MEM_H(0, static_cast<int32_t>(0x800DF468)) == 0) {
        wdc::rumble::stop();
        return;
    }
    auto previous = motor_until.load(std::memory_order_relaxed);
    if (previous != 0) motor_until.compare_exchange_strong(previous, now_ms() + LeaseMs,
        std::memory_order_relaxed);
}
