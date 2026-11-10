#undef NDEBUG
#include "recomp.h"
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
std::vector<unsigned> calls;
unsigned views = 0, prepared = 0, drawn = 0;
void called(unsigned address) { calls.push_back(address); }
}
extern "C" {
void func_8000A30C(uint8_t*, recomp_context*);
void func_80025350(uint8_t*, recomp_context*);
void func_80001D34(uint8_t*, recomp_context* ctx) {
    assert(uint32_t(ctx->r4) == 0x8000A240);
    called(0x80001D34);
}
void func_80010A5C(uint8_t*, recomp_context* ctx) {
    assert(uint32_t(ctx->r4) == 0x800ABE40 + prepared * 0x1418);
    ++prepared;
    called(0x80010A5C);
}
void func_80010ECC(uint8_t*, recomp_context* ctx) {
    assert(prepared == views);
    assert(uint32_t(ctx->r4) == 0x800ABE40 + drawn * 0x1418);
    ++drawn;
    called(0x80010ECC);
}
void func_80024FCC(uint8_t*, recomp_context*) {
    assert(prepared == views && drawn == views);
    called(0x80024FCC);
}
void func_800214C8(uint8_t* rdram, recomp_context*) {
    assert(calls.back() == 0x80024FCC);
    // The real cleanup runs between submission and this audio consumer.
    assert(MEM_W(0, int32_t(0x8007ED78)) == 0);
    called(0x800214C8);
}
#define OBSERVE(name, address) \
    void name(uint8_t*, recomp_context*) { called(address); }
OBSERVE(func_80024038, 0x80024038)
OBSERVE(func_8004D020, 0x8004D020)
OBSERVE(func_80044FA8, 0x80044FA8)
OBSERVE(func_80046D20, 0x80046D20)
OBSERVE(func_80024094, 0x80024094)
OBSERVE(func_80024D38, 0x80024D38)
OBSERVE(func_800245AC, 0x800245AC)
OBSERVE(func_8002430C, 0x8002430C)
OBSERVE(func_8004D120, 0x8004D120)
OBSERVE(func_80044FC8, 0x80044FC8)
OBSERVE(func_80046D40, 0x80046D40)
OBSERVE(func_80024364, 0x80024364)
OBSERVE(func_800241C0, 0x800241C0)
#undef OBSERVE
}

int main() {
    // Synthetic 8 MiB word-swapped RDRAM, owned by this single-threaded test.
    // MEM_W/H/B supply the guest's big-endian word/halfword/byte accesses.
    // All records and the guest stack below are fixed, aligned and in bounds.
    for (views = 0; views <= 2; ++views) {
        std::vector<uint8_t> memory(8 * 1024 * 1024);
        auto* rdram = memory.data();
        recomp_context ctx{};
        ctx.r29 = int32_t(0x807FF000);
        ctx.r31 = int32_t(0x8000C878);
        ctx.r16 = 123;
        const auto original_sp = ctx.r29, original_ra = ctx.r31;
        MEM_W(0, int32_t(0x8007E574)) = views;
        MEM_W(0, int32_t(0x80080D90)) = 2; // Two cars, independently of views.
        MEM_H(0, int32_t(0x8007E868)) = -1; // No pending sound to enqueue.
        MEM_W(0, int32_t(0x8007EDBC)) = 0; // No clear rectangle this frame.
        MEM_W(0, int32_t(0x8007ED78)) = 17;
        MEM_B(0, int32_t(0x8007E834)) = 7;
        MEM_W(0, int32_t(0x8007E860)) = 0x800B3000;
        for (unsigned car = 0; car < 2; ++car) {
            MEM_W(0, int32_t(0x800BA7F0 + car * 0x18A0 + 0x9F8)) = 42;
            MEM_W(0, int32_t(0x800BA7F0 + car * 0x18A0 + 0x9FC)) = 43;
        }
        // Simulation clock/sample count sentinels. No callee in this fixture
        // consumes input or advances physics; that is not a full-graph proof.
        MEM_W(0, int32_t(0x8007E530)) = 99;
        MEM_W(0, int32_t(0x8007FE14)) = 3;
        calls.clear();
        prepared = drawn = 0;
        func_8000A30C(rdram, &ctx);
        assert(ctx.r29 == original_sp && ctx.r31 == original_ra);
        assert(ctx.r16 == 123);
        assert(MEM_W(0, int32_t(0x8007E530)) == 99);
        assert(MEM_W(0, int32_t(0x8007FE14)) == 3);
        for (unsigned car = 0; car < 2; ++car) {
            assert(MEM_W(0, int32_t(0x800BA7F0 + car * 0x18A0 + 0x9F8)) == 0);
            assert(MEM_W(0, int32_t(0x800BA7F0 + car * 0x18A0 + 0x9FC)) == 0);
        }
        // Actual sound-list rollover: rendering twice is observably different,
        // even with unchanged simulation and all drawing helpers stubbed.
        assert(MEM_BU(0, int32_t(0x8007E84C)) == 7);
        assert(MEM_W(0, int32_t(0x8007E864)) == int32_t(0x800B3000));
        assert(MEM_BU(0, int32_t(0x8007E844)) == 1);
        assert(MEM_W(0, int32_t(0x8007E85C)) == int32_t(0x800B1BD0));
        std::vector<unsigned> expected{0x80001D34, 0x80024038};
        for (unsigned i = 0; i < views; ++i)
            expected.insert(expected.end(), {0x80010A5C, 0x8004D020});
        expected.insert(expected.end(), {0x80044FA8, 0x80046D20, 0x80024094, 0x80024D38});
        for (unsigned i = 0; i < views; ++i)
            expected.insert(expected.end(), {0x80010ECC, 0x8004D120});
        expected.insert(expected.end(), {0x80044FC8, 0x80046D40, 0x80024FCC, 0x800214C8});
        assert(calls == expected);
        prepared = drawn = 0;
        func_8000A30C(rdram, &ctx);
        assert(MEM_BU(0, int32_t(0x8007E84C)) == 0);
        assert(MEM_W(0, int32_t(0x8007E864)) == 0);
        assert(MEM_BU(0, int32_t(0x8007E844)) == 0);
        assert(MEM_W(0, int32_t(0x8007E85C)) == int32_t(0x800B1E50));
        MEM_W(0, int32_t(0x8007EDBC)) = 3;
        for (int remaining : {2, 1, 0, 0}) {
            MEM_W(0, int32_t(0x8007ED78)) = 17;
            func_80025350(rdram, &ctx);
            assert(MEM_W(0, int32_t(0x8007EDBC)) == remaining);
            assert(MEM_W(0, int32_t(0x8007ED78)) == 0);
        }
    }
}
