#undef NDEBUG
#include "recomp.h"
#include <algorithm>
#include <cassert>
#include <vector>

// Exercise the actual startup and ring initializers. Model an immediate
// preemption at osStartThread: every controller record must already be ready.
namespace {
unsigned starts = 0, creates = 0;
}
extern "C" {
void func_8002CB0C(uint8_t*, recomp_context*);
void osCreateMesgQueue_recomp(uint8_t*, recomp_context*) {}
void osSendMesg_recomp(uint8_t*, recomp_context*) {}
void func_8004D79C(uint8_t*, recomp_context*) {}
void osCreateThread_recomp(uint8_t*, recomp_context* ctx) {
    ++creates;
    assert(uint32_t(ctx->r4) == 0x800B7620);
    assert(ctx->r5 == 9 && uint32_t(ctx->r6) == 0x8002C500);
}
void osStartThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ++starts;
    assert(creates == starts);
    assert(uint32_t(ctx->r4) == 0x800B7620);
    for (unsigned slot = 0; slot < 8; ++slot) {
        const int32_t base = int32_t(0x800B97E8 + slot * 0x200);
        for (unsigned port = 0; port < 4; ++port) {
            const int32_t record = base + port * 0x7C;
            assert(uint32_t(MEM_W(0x74, record)) == 0x800AAAE0 + port * 6);
            assert(MEM_W(0x78, record) == base);
        }
    }
}
}
int main() {
    std::vector<uint8_t> storage(8 * 1024 * 1024);
    auto* rdram = storage.data();
    for (unsigned run = 0; run < 2; ++run) {
        // Poison the ring on restart so stale pointers cannot hide ordering.
        std::fill(storage.begin() + 0xB97E8, storage.begin() + 0xBA7E8, 0);
        recomp_context ctx{};
        ctx.r29 = int32_t(0x807FF000);
        ctx.r16 = 0x1234;
        ctx.r17 = 0x5678;
        ctx.r5 = run;
        func_8002CB0C(rdram, &ctx);
        assert(starts == run + 1);
        assert(ctx.r29 == gpr(int32_t(0x807FF000)));
        assert(ctx.r16 == 0x1234 && ctx.r17 == 0x5678);
    }
}
