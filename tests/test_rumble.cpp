#undef NDEBUG
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>
#include "rumble.h"
#include "recomp.h"

extern "C" {
void wdc_motor_init(uint8_t*, recomp_context*);
void wdc_motor_start(uint8_t*, recomp_context*);
void wdc_motor_stop(uint8_t*, recomp_context*);
void wdc_rumble_tick(uint8_t*, recomp_context*);
}
int main() {
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
    wdc_motor_init(rdram, &ctx);
    assert(ctx.r2 == 0 && !wdc::rumble::active());
    wdc_motor_start(rdram, &ctx);
    assert(ctx.r2 == 0 && wdc::rumble::active());
    ctx.r4 = static_cast<int32_t>(0x800AAAF8);
    wdc_motor_stop(rdram, &ctx);
    assert(ctx.r2 == 5 && wdc::rumble::active());
    assert(memory == original); // Motor adapters never alter filesystem/guest RAM.
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
