#include "rumble.h"
#include "recomp.h"
#include <atomic>
#include <chrono>

namespace {
std::atomic<uint64_t> motor_until{};
std::atomic<bool> initialized{};
constexpr uint32_t SharedPfs = 0x800AAC98;
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

// USA adapter, synchronous on the input worker. The 104-byte OSPfs at 800AAC98
// is shared by filesystem and motor calls. Only this fixed object is writable,
// within the runtime's 8 MiB RDRAM. Init stores big-endian u32 status/queue/channel
// at +0/+4/+8 and u8 activebank at +0x65 using word-swapped MEM helpers. Queue a0
// is an SDK-owned guest address token and is never dereferenced; channel a2 is
// zero-based. No guest pointer is retained. Fields mirror runtime initialization, except
// motor bit 8 is added to status, preserving filesystem flags and ID/layout
// metadata. Bank FF invalidates the SDK's bank cache; its next file call selects
// bank 0. The auxiliary probe object at 800AAAF8 must fail with DEVICE (11),
// preserving the Controller Pak UI. Invalid pointers return 11 and absent
// channels return CONTRFAIL (4), without writes; uninitialized Start/Stop return INVALID
// (5). Start/Stop use host initialization state so filesystem reinitialization
// cannot disable the virtual motor. Motor calls never change the Pak image.
// Replace this address identity with a named accessory API when decompiled.
extern "C" void wdc_motor_init(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 4;
    if (ctx->r6 != 0) return;
    ctx->r2 = 11;
    if (static_cast<uint32_t>(ctx->r5) != SharedPfs) return;
    const gpr pfs = static_cast<int32_t>(SharedPfs);
    MEM_W(4, pfs) = ctx->r4;
    MEM_W(8, pfs) = 0;
    MEM_B(0x65, pfs) = 0xFF;
    MEM_W(0, pfs) |= 8; // Retain PFS_INITIALIZED and the filesystem's other status bits.
    initialized.store(true, std::memory_order_relaxed);
    wdc::rumble::stop();
    ctx->r2 = 0;
}
extern "C" void wdc_motor_start(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 5;
    if (static_cast<uint32_t>(ctx->r4) != SharedPfs || !initialized.load(std::memory_order_relaxed)) return;
    wdc::rumble::set_motor(0, true);
    ctx->r2 = 0;
}
extern "C" void wdc_motor_stop(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 5;
    if (static_cast<uint32_t>(ctx->r4) != SharedPfs || !initialized.load(std::memory_order_relaxed)) return;
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
