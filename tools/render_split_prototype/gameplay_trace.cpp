// Test-only MinGW player fixture. Record the authoritative player car after
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
    // USA single-car practice, game thread. The aligned u32 sample counter
    // and sixteen raw words at car BA7F0 + 14 are copied before another tick.
    const uint32_t sample = MEM_W(0, int32_t(0x8007E530));
    if (!out || std::fwrite(&sample, sizeof(sample), 1, out) != 1 ||
        std::fwrite(rdram + 0xBA804, 64, 1, out) != 1) std::_Exit(7);
    if (++recorded == 180) {
        const int status = std::fclose(out);
        std::fflush(nullptr);
        std::_Exit(status ? 7 : 0);
    }
}
}
