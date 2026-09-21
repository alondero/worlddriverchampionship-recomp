#include <cmath>
#include <bit>
#include "recomp.h"
#include "guest_memory.h"

// ROM assembly shares square/square-root tails using a computed return address.
// Preserve its float evaluation order and caller-visible scratch registers.
static void vector_magnitude(uint8_t* rdram, recomp_context* ctx, bool distance,
                             bool three, uint32_t continuation) {
    wdc_require_guest_range(ctx->r4, three ? 12 : 8, 4);
    if (distance) wdc_require_guest_range(ctx->r5, three ? 12 : 8, 4);
    auto read = [&](gpr address, int offset) { return std::bit_cast<float>(static_cast<uint32_t>(MEM_W(offset, address))); };
    float x = read(ctx->r4, 0), y = read(ctx->r4, 4);
    float z = three ? read(ctx->r4, 8) : 0.0f;
    if (distance) {
        ctx->f10.fl = read(ctx->r5, 0);
        ctx->f16.fl = read(ctx->r5, 4);
        x = ctx->f10.fl - x;
        y = ctx->f16.fl - y;
        if (three) {
            ctx->f18.fl = read(ctx->r5, 8);
            z = ctx->f18.fl - z;
        }
    }
    ctx->f4.fl = x;
    ctx->f6.fl = y * y;
    float square = x * x + ctx->f6.fl;
    if (three) { ctx->f8.fl = z * z; square += ctx->f8.fl; }
    ctx->f0.fl = std::sqrt(square);
    ctx->r25 = ctx->r31;
    ctx->r31 = static_cast<int32_t>(continuation);
}
extern "C" void wdc_length3(uint8_t* r, recomp_context* c) { vector_magnitude(r,c,false,true,0x80019FF0); }
extern "C" void wdc_distance3(uint8_t* r, recomp_context* c) { vector_magnitude(r,c,true,true,0x8001A268); }
extern "C" void wdc_distance2(uint8_t* r, recomp_context* c) { vector_magnitude(r,c,true,false,0x8001A5A8); }
extern "C" void wdc_length2(uint8_t* r, recomp_context* c) { vector_magnitude(r,c,false,false,0x8001A5E8); }

// The ROM entry jumps backwards into a two-instruction shared loop prefix.
extern "C" void wdc_compare_string(uint8_t* rdram, recomp_context* ctx) {
    do {
        wdc_require_guest_range(ctx->r4, 1);
        wdc_require_guest_range(ctx->r5, 1);
        ctx->r8 = MEM_BU(0, ctx->r4);
        ctx->r9 = MEM_BU(0, ctx->r5);
        ctx->r4 = ADD32(ctx->r4, 1);
        if (ctx->r8 != ctx->r9) {
            ctx->r2 = ctx->r8 < ctx->r9 ? -1 : 1;
            return;
        }
        ctx->r5 = ADD32(ctx->r5, 1);
    } while (ctx->r8);
    ctx->r2 = 0;
}
