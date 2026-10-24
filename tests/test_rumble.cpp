#undef NDEBUG
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include "rumble.h"
#include "ui/rumble_output.h"
#include "recomp.h"

extern "C" {
void wdc_motor_init(uint8_t*, recomp_context*);
void wdc_motor_start(uint8_t*, recomp_context*);
void wdc_motor_stop(uint8_t*, recomp_context*);
void wdc_rumble_tick(uint8_t*, recomp_context*);
}
int main() {
    wdc::input::RumbleOutput output;
    std::vector<uint16_t> writes;
    auto send = [&](uint16_t strength) {
        writes.push_back(strength);
        return strength == 0 ? -1 : 0; // A transient stop failure, not an unsupported pad.
    };
    output.apply(0, send); output.apply(0, send);
    assert(writes.empty()); // Idle must not send anything, even if the driver would deduplicate.
    output.apply(123, send); output.apply(123, send);
    assert((writes == std::vector<uint16_t>{123, 123})); // Refresh the short device lease.
    output.apply(0, send); output.apply(0, send);
    assert((writes == std::vector<uint16_t>{123, 123, 0})); // Exactly one stop, including failure.
    output.apply(456, send);
    assert(writes.back() == 456); // A failed stop cannot disable the next effect.
    wdc::input::RumbleOutput unsupported;
    int failures = 0;
    auto fail = [&](uint16_t) { ++failures; return -1; };
    unsupported.apply(123, fail); unsupported.apply(123, fail);
    assert(failures == 1);
    unsupported.apply(0, fail); unsupported.apply(0, fail);
    assert(failures == 2); // Still attempt one stop after a failed active command.
    std::vector<uint8_t> memory(8 * 1024 * 1024, 0xA5);
    auto* rdram = memory.data();
    const auto original = memory;
    recomp_context ctx{};
    ctx.r4 = static_cast<int32_t>(0x800AAC98);
    wdc_motor_start(rdram, &ctx);
    assert(ctx.r2 == 5 && !wdc::rumble::active());
    for (uint32_t pointer : {0u, 0x800AAAF8u, 0x807FFFFFu}) {
        ctx.r5 = static_cast<int32_t>(pointer);
        wdc_motor_init(rdram, &ctx);
        assert(ctx.r2 == 11);
    }
    ctx.r5 = static_cast<int32_t>(0x800AAC98);
    for (int channel : {-1, 1, 4}) {
        ctx.r6 = channel;
        wdc_motor_init(rdram, &ctx);
        assert(ctx.r2 == 4);
    }
    ctx.r6 = 0;
    ctx.r4 = static_cast<int32_t>(0x800AAAB0);
    wdc_motor_init(rdram, &ctx);
    assert(ctx.r2 == 0 && !wdc::rumble::active());
    const gpr pfs = static_cast<int32_t>(0x800AAC98);
    assert(MEM_W(4, pfs) == static_cast<int32_t>(0x800AAAB0));
    assert(MEM_W(8, pfs) == 0 && MEM_BU(0x65, pfs) == 0xFF);
    assert(static_cast<uint32_t>(MEM_W(0, pfs)) == (0xA5A5A5A5u | 8));
    ctx.r4 = pfs;
    wdc_motor_start(rdram, &ctx);
    assert(ctx.r2 == 0 && wdc::rumble::active());
    ctx.r4 = static_cast<int32_t>(0x800AAAF8);
    wdc_motor_stop(rdram, &ctx);
    assert(ctx.r2 == 5 && wdc::rumble::active());
    auto expected = original;
    for (unsigned i : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 0x65u})
        expected[(0xAAC98 + i) ^ 3u] = memory[(0xAAC98 + i) ^ 3u];
    assert(memory == expected); // Metadata, adjacent objects and all other RAM are preserved.
    ctx.r4 = static_cast<int32_t>(0x800AAC98);
    wdc_motor_stop(rdram, &ctx);
    assert(ctx.r2 == 0 && !wdc::rumble::active());
    wdc::rumble::set_motor(-1, true);
    wdc::rumble::set_motor(1, true);
    assert(!wdc::rumble::active());
    wdc::rumble::set_motor(0, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    assert(!wdc::rumble::active());
    MEM_H(0, static_cast<int32_t>(0x800DF464)) = 1;
    MEM_H(0, static_cast<int32_t>(0x800DF468)) = 1;
    ctx.r17 = 1;
    wdc_rumble_tick(rdram, &ctx);
    assert(!wdc::rumble::active());
    ctx.r17 = 0;
    wdc_rumble_tick(rdram, &ctx);
    assert(wdc::rumble::active());
    wdc::rumble::stop();
    wdc_rumble_tick(rdram, &ctx);
    assert(!wdc::rumble::active()); // Refresh cannot undo an SDL-thread mute.
    wdc_motor_start(rdram, &ctx);
    MEM_H(0, static_cast<int32_t>(0x800DF464)) = 0;
    wdc_rumble_tick(rdram, &ctx);
    assert(!wdc::rumble::active()); // Guest effect reset cancels a held motor.
}
