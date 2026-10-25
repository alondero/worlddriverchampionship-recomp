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

// USA virtual-motor boundary, synchronous on any calling guest thread. a1
// (Init) / a0 (Start/Stop) is a 32-bit identity token for the primary OSPfs at
// 800AAC98; channel a2 is zero-based. Queue a0 is unused. These three adapters
// read/write NO guest memory and retain no pointer; only host atomics cross
// threads. File routines may overlap: no SDK lock or guest-field ordering is
// needed because the filesystem remains the sole owner of its entire object.
// The physical ROM initializer clears status and selects bank 80, which is
// incompatible with simultaneous memory-Pak service. Do not transplant those
// writes, runtime status bit 8, or a bank sentinel into the shared filesystem.
// All translated motor field readers (the SDK Start/Stop bodies) are replaced
// by these host-state adapters. See the original-ROM comparison in the fixture.
// The auxiliary probe at 800AAAF8 returns DEVICE (11), preserving the save UI.
// Invalid tokens return 11 and absent channels return CONTRFAIL (4), with no
// memory access; uninitialized Start/Stop return INVALID (5). File-system
// reinitialization cannot disable the independent virtual motor.
// Replace this address identity with a named accessory API when decompiled.
extern "C" void wdc_motor_init(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 4;
    if (ctx->r6 != 0) return;
    ctx->r2 = 11;
    if (static_cast<uint32_t>(ctx->r5) != SharedPfs) return;
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
