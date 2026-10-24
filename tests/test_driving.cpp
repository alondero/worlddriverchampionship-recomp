#undef NDEBUG
#include "game_driving.h"
#include "recomp.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

extern "C" {
void wdc_driving_input_flags(uint8_t*, recomp_context*);
void wdc_analog_throttle(uint8_t*, recomp_context*);
void wdc_analog_brake(uint8_t*, recomp_context*);
}
int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    using namespace wdc::driving;
    assert(read(-1).throttle == 0 && read(4).brake == 0);
    publish(-1, {1, 1}); publish(4, {1, 1});
    publish(0, {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()});
    assert(read(0).throttle == 0 && read(0).brake == 0);
    publish(0, {-1, 2});
    assert(read(0).throttle == 0 && read(0).brake == 1);
    // Every ring slot and port, including the padding between 0x200-byte slots.
    for (int slot = 0; slot < 8; ++slot) {
        for (int port = 0; port < 4; ++port) {
            const int32_t record = int32_t(0x800B97E8 + slot * 0x200 + port * 0x7C);
            MEM_W(0x74, record) = int32_t(0x800AAAE0 + port * 6);
            MEM_W(0, record) = 0x108; // unrelated menu bits
            publish(port, {0.25f, 0.5f});
            recomp_context ctx{};
            ctx.r4 = record;
            wdc_driving_input_flags(rdram, &ctx);
            assert(uint32_t(MEM_W(0, record)) == 0x60050108);
            ctx.r8 = record; ctx.r7 = 0x3F800000;
            wdc_analog_throttle(rdram, &ctx);
            assert(ctx.r7 == 0x3E800000);
            ctx.f2.fl = 100; ctx.f0.fl = 80;
            wdc_analog_brake(rdram, &ctx);
            assert(ctx.f2.fl == 60 && ctx.f0.fl == 40);
            MEM_W(0, record) |= 0x30; // digital A/B override even a stale tag
            ctx.r7 = 0x3F800000; ctx.f2.fl = 100; ctx.f0.fl = 80;
            wdc_analog_throttle(rdram, &ctx); wdc_analog_brake(rdram, &ctx);
            assert(ctx.r7 == 0x3F800000 && ctx.f2.fl == 100 && ctx.f0.fl == 80);
        }
    }
    for (uint32_t address : {0u, 0x807FFFFCu, 0x800B97E9u, 0x800B99D8u, 0x800BA7E8u}) {
        recomp_context ctx{};
        ctx.r4 = int32_t(address); ctx.r8 = ctx.r4;
        ctx.r7 = 0x3F800000; ctx.f2.fl = 100; ctx.f0.fl = 80;
        wdc_driving_input_flags(rdram, &ctx);
        wdc_analog_throttle(rdram, &ctx); wdc_analog_brake(rdram, &ctx);
        assert(ctx.r7 == 0x3F800000 && ctx.f2.fl == 100 && ctx.f0.fl == 80);
    }
    const int32_t record = int32_t(0x800B97E8);
    recomp_context ctx{};
    ctx.r4 = record; ctx.r8 = record;
    MEM_W(0x74, record) = int32_t(0x800AAAE0);
    MEM_W(0, record) = 0;
    MEM_B(0x27, record) = 1;
    wdc_driving_input_flags(rdram, &ctx);
    assert(MEM_W(0, record) == 0);
    MEM_B(0x27, record) = 0;
    MEM_W(0x28, record) = 1;
    wdc_driving_input_flags(rdram, &ctx);
    assert(MEM_W(0, record) == 0);
    MEM_W(0x28, record) = 0;
    MEM_W(0x74, record) = int32_t(0x800AAAE6); // wrong port's pointer
    wdc_driving_input_flags(rdram, &ctx);
    assert(MEM_W(0, record) == 0);
    MEM_W(0x74, record) = int32_t(0x800AAAE0);
    MEM_W(0, int32_t(0x8007DF94)) = 2;
    wdc_driving_input_flags(rdram, &ctx);
    assert(MEM_W(0, record) == 0);
    MEM_W(0, record) = 0x60050000;
    ctx.r7 = 0x3F800000; ctx.f2.fl = 100; ctx.f0.fl = 80;
    wdc_analog_throttle(rdram, &ctx); wdc_analog_brake(rdram, &ctx);
    assert(ctx.r7 == 0x3F800000 && ctx.f2.fl == 100 && ctx.f0.fl == 80);
}
