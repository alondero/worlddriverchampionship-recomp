#include "recomp.h"

// Host-only equivalents of the three leaf accessors used by the warp boundary.
// ROM-backed tests extract the real translated bodies instead.
extern "C" void func_800691B0(uint8_t* rdram, recomp_context* ctx) {
    const int64_t model = int32_t(MEM_W(0x106C, int32_t(ctx->r4)));
    ctx->r2 = gpr(int32_t(ctx->r4 + 0xB4C + model * 20));
}
extern "C" void func_800563FC(uint8_t* rdram, recomp_context* ctx) {
    const int64_t model = int32_t(MEM_W(8, int32_t(ctx->r4)));
    ctx->r2 = gpr(int32_t(MEM_W(0x10A8 + model * 0x80, int32_t(0x80080000))));
}
extern "C" void func_80066C7C(uint8_t*, recomp_context* ctx) {
    ctx->r2 = gpr(int32_t(ctx->r4 + 0x2C + int32_t(ctx->r5) * 40));
}
