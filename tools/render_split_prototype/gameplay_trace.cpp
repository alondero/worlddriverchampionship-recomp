// Test-only MinGW player fixture. Record the authoritative race cars after
// each 8000C6E8 physics call. Extra draws never reach that dispatcher site.
#include "recomp.h"
#include "savestate.h"
#include <cstdio>
#include <cstdlib>

extern "C" {
void func_8000A30C(uint8_t*, recomp_context*);
void __real_wdc_render_race_draw(uint8_t*, recomp_context*);
void __real_osContGetReadData_recomp(uint8_t*, recomp_context*);

void __wrap_osContGetReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    __real_osContGetReadData_recomp(rdram, ctx);
    // Game controller worker, after SDK sampling. Select only the port-zero
    // pad at the known 24-byte array. The guest button u16 and signed stick
    // bytes use word-swapped accessors. Preserve error and replay handling.
    const auto pad = int32_t(ctx->r4);
    if (uint32_t(pad) != 0x800AAAE0 || uint32_t(MEM_W(0, int32_t(0x8007D848))) != 0x8000C560 ||
        MEM_W(0, int32_t(0x8007DF94)) == 2 || MEM_BU(4, pad)) return;
    MEM_H(0, pad) = 0x8000;
    MEM_B(2, pad) = 0;
    MEM_B(3, pad) = 0;
}
// Boot each trial with the original queue layout required by the shared slot.
// After loading, execute the ordinary player path at the configured rate.
void __wrap_wdc_render_race_draw(uint8_t* rdram, recomp_context* ctx) {
    if (wdc::state::restore_generation()) __real_wdc_render_race_draw(rdram, ctx);
    else func_8000A30C(rdram, ctx);
}
void compare_physics(uint8_t* rdram) {
    if (!wdc::state::restore_generation()) return;
    static FILE* out = std::fopen("physics.bin", "wb");
    static unsigned recorded = 0;
    // USA race roster, game thread. Aligned u32 words use host byte order
    // in word-swapped RDRAM. Each car's sixteen transform words start at
    // BA7F0 + 14, with stride 18A0. Validate the roster against 8 MiB RAM.
    const uint32_t cars = MEM_W(0, int32_t(0x80080D90));
    static const uint32_t roster = cars;
    if (!out || !cars || cars > (0x800000u - 0xBA7F0u) / 0x18A0u ||
        cars != roster) std::_Exit(7);
    if (!recorded && std::fwrite(&cars, sizeof(cars), 1, out) != 1) std::_Exit(7);
    const uint32_t sample = MEM_W(0, int32_t(0x8007E530));
    if (std::fwrite(&sample, sizeof(sample), 1, out) != 1) std::_Exit(7);
    for (uint32_t car = 0; car < cars; ++car) {
        if (std::fwrite(rdram + 0xBA804 + car * 0x18A0, 64, 1, out) != 1)
            std::_Exit(7);
    }
    if (++recorded == 180) {
        const int status = std::fclose(out);
        std::fflush(nullptr);
        std::_Exit(status ? 7 : 0);
    }
}
}
