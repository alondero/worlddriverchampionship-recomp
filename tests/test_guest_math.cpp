#include <bit>
#include <cstdio>
#include <vector>
#include "recomp.h"
extern "C" void wdc_length3(uint8_t*, recomp_context*);
extern "C" void wdc_length2(uint8_t*, recomp_context*);
extern "C" void wdc_distance3(uint8_t*, recomp_context*);
extern "C" void wdc_distance2(uint8_t*, recomp_context*);
extern "C" void wdc_compare_string(uint8_t*, recomp_context*);
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Failed: %s\n", #x); return 1; } } while (0)
int main() {
    std::vector<uint8_t> memory(4096);
    uint8_t* rdram = memory.data();
    recomp_context ctx{};
    ctx.r4 = int32_t(0x80000100); ctx.r5 = int32_t(0x80000200);
    auto put = [&](gpr addr, int offset, float value) { MEM_W(offset, addr) = std::bit_cast<uint32_t>(value); };
    put(ctx.r4,0,3); put(ctx.r4,4,4); put(ctx.r4,8,12);
    ctx.r31 = 0x1234;
    wdc_length3(rdram,&ctx);
    CHECK(ctx.f0.fl == 13 && ctx.r25 == 0x1234 && ctx.f8.fl == 144);
    wdc_length2(rdram,&ctx); CHECK(ctx.f0.fl == 5);
    put(ctx.r5,0,6); put(ctx.r5,4,8); put(ctx.r5,8,24);
    wdc_distance3(rdram,&ctx); CHECK(ctx.f0.fl == 13);
    wdc_distance2(rdram,&ctx); CHECK(ctx.f0.fl == 5);
    wdc_distance3(rdram,&ctx); CHECK(ctx.f4.fl == 3 && ctx.f6.fl == 16);
    MEM_B(0,ctx.r4) = 0x80; MEM_B(0,ctx.r5) = 0x7f;
    wdc_compare_string(rdram,&ctx); CHECK(ctx.r2 == 1);
    ctx.r4 = int32_t(0x80000100); ctx.r5 = int32_t(0x80000200);
    MEM_B(0,ctx.r4) = 'a'; MEM_B(0,ctx.r5) = 'b';
    wdc_compare_string(rdram,&ctx); CHECK(int64_t(ctx.r2) == -1);
    ctx.r4 = int32_t(0x80000100); ctx.r5 = int32_t(0x80000200);
    MEM_B(0,ctx.r4) = 'a'; MEM_B(0,ctx.r5) = 'a';
    MEM_B(1,ctx.r4) = 0; MEM_B(1,ctx.r5) = 0;
    wdc_compare_string(rdram,&ctx); CHECK(ctx.r2 == 0);
    CHECK(ctx.r4 == gpr(int32_t(0x80000102)) && ctx.r5 == gpr(int32_t(0x80000202)));
    return 0;
}
