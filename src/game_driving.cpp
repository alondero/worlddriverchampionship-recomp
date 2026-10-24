#include "game_driving.h"
#include "recomp.h"
#ifdef WDC_ENABLE_FRONTEND
#include "ui/frontend.h"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>

namespace {
std::array<std::atomic<uint64_t>, 4> pedals{};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
constexpr uint32_t analog_throttle = 0x20000000, analog_brake = 0x40000000;
float normalized(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }
bool record_valid(uint32_t record) {
    if (record < 0x800B97E8 || record >= 0x800B97E8 + 8 * 0x200) return false;
    const auto offset = (record - 0x800B97E8) % 0x200;
    return offset < 4 * 0x7C && offset % 0x7C == 0;
}
int record_port(uint8_t* rdram, uint32_t record) {
    if (!record_valid(record)) return -1;
    const uint32_t pad = MEM_W(0x74, int32_t(record));
    const int port = int(((record - 0x800B97E8) % 0x200) / 0x7C);
    return pad == 0x800AAAE0 + port * 6 ? port : -1;
}
bool playback(uint8_t* rdram) { return MEM_W(0, int32_t(0x8007DF94)) == 2; }
}
void wdc::driving::publish(int port, Pedals value) {
    if (port < 0 || port >= int(pedals.size())) return;
    pedals[port].store(uint64_t(std::bit_cast<uint32_t>(normalized(value.throttle))) |
        (uint64_t(std::bit_cast<uint32_t>(normalized(value.brake))) << 32), std::memory_order_release);
}
wdc::driving::Pedals wdc::driving::read(int port) {
    if (port < 0 || port >= int(pedals.size())) return {};
#ifdef WDC_ENABLE_FRONTEND
    if (wdc::ui::captures_input()) return {};
#endif
    const uint64_t value = pedals[port].load(std::memory_order_acquire);
    return {std::bit_cast<float>(uint32_t(value)), std::bit_cast<float>(uint32_t(value >> 32))};
}

// USA controller worker, func_80002F58 before 800031B4: a0 is a game-owned
// 0x7C-byte record at 800B97E8 + slot*0x200 + port*0x7C (8 slots, 4 ports).
// +0 is a u32 action mask,
// +25 is signed stick Y (N64 units), +27 disables driving actions, +28 is
// controller error, and +74 is a u32 OSContPad pointer into 800AAAE0 + port*6.
// Big-endian guest fields use word-swapped MEM helpers; all addresses/record
// alignment and pad ports are checked. Invalid/disabled/error records and
// replay mode (u32 at 8007DF94) retain ROM behavior. Only high action bits are
// added: low menu/button bits stay intact. Bits 29/30 tag synthetic pedals;
// the ROM rebuilds/overwrites this word on polling, replay and demo playback.
// No guest pointer survives this call; host samples are lock-free atomics.
// Replace these bridges with named input/drivetrain APIs after decompilation.
extern "C" void wdc_driving_input_flags(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t record = uint32_t(ctx->r4);
    const int port = record_port(rdram, record);
    if (port < 0 || playback(rdram)) return;
    if (MEM_BU(0x27, int32_t(record)) || MEM_W(0x28, int32_t(record))) return;
    const auto value = wdc::driving::read(port);
    uint32_t flags = MEM_W(0, int32_t(record));
    if (value.throttle > 0 && !(flags & 0x10)) {
        flags |= analog_throttle | 0x10000;
        if (MEM_B(0x25, int32_t(record)) < -40) flags |= 0x20000;
    }
    if (value.brake > 0 && !(flags & 0x20)) flags |= analog_brake | 0x40000;
    MEM_W(0, int32_t(record)) = flags;
}

// USA human drivetrain, func_80016DF8 before 80016EB4 / 80017078. t0 is
// the same validated ring record; its mask is read-only here. Other records
// (AI), replay, or untagged digital A/B retain ROM operands. a3 carries IEEE
// float32 throttle [0,1] to func_80017530. f2/f0 carry front/rear wheel torque
// in ROM units, freshly calculated for this update. Preserve the stock front
// rolling-resistance baseline min(20, full torque); rear baseline is zero.
// Guest simulation thread, register writes only, no retained guest pointers.
extern "C" void wdc_analog_throttle(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t record = uint32_t(ctx->r8);
    const int port = record_port(rdram, record);
    if (port < 0 || playback(rdram)) return;
    const uint32_t flags = MEM_W(0, int32_t(record));
    if ((flags & analog_throttle) && !(flags & 0x10))
        ctx->r7 = int32_t(std::bit_cast<uint32_t>(wdc::driving::read(port).throttle));
}
extern "C" void wdc_analog_brake(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t record = uint32_t(ctx->r8);
    const int port = record_port(rdram, record);
    if (port < 0 || playback(rdram)) return;
    const uint32_t flags = MEM_W(0, int32_t(record));
    if (!(flags & analog_brake) || (flags & 0x20)) return;
    const float brake = wdc::driving::read(port).brake;
    const float baseline = std::min(20.0f, ctx->f2.fl);
    ctx->f2.fl = baseline + brake * (ctx->f2.fl - baseline);
    ctx->f0.fl *= brake;
}
