#undef NDEBUG
#include "game_cheats.h"
#include "recomp.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
void wdc_cheat_top_down_begin(uint8_t*, recomp_context*);
void wdc_cheat_top_down_end(uint8_t*, recomp_context*);
uint64_t wdc_cheat_cinematic(uint8_t*, recomp_context*, uint64_t);
}

namespace {
constexpr int32_t state = int32_t(0x8007D848);
constexpr std::array<uint32_t, 2> scenes{0x800ABE40, 0x800AE670};
constexpr std::array<int32_t, 5> offsets{0x94, 0x9C, 0xA0, 0xA4, 0xA8};
uint32_t bits(float value) {
    uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
}

int main() {
    using wdc::cheats::Cheat;
    std::vector<uint8_t> memory(8 * 1024 * 1024, 0x5A);
    auto* rdram = memory.data();
    MEM_W(0, state) = int32_t(0x8000C560);
    recomp_context ctx{};
    ctx.r29 = int32_t(0x807FF000);
    for (const auto scene : scenes) {
        const auto camera = int32_t(scene + 0x11C);
        MEM_W(0x8C, camera) = bits(2.0f);
        // Preserve arbitrary native settings bit for bit, including NaN payloads.
        const std::array<uint32_t, 5> original{
            bits(3.0f), bits(5.0f), 0x7FC12345, 0x80000000, bits(9.0f)};
        for (std::size_t i = 0; i < offsets.size(); ++i)
            MEM_W(offsets[i], camera) = original[i];
        ctx.r4 = camera;
        const auto baseline = memory;
        wdc::cheats::set_enabled(Cheat::TopDown, false);
        wdc_cheat_top_down_begin(rdram, &ctx);
        wdc_cheat_top_down_end(rdram, &ctx);
        assert(memory == baseline);

        wdc::cheats::set_enabled(Cheat::TopDown, true);
        wdc_cheat_top_down_begin(rdram, &ctx);
        assert(uint32_t(MEM_W(0x94, camera)) == bits(26.0f));
        assert(uint32_t(MEM_W(0x9C, camera)) == bits(22.0f));
        assert(MEM_W(0xA0, camera) == 0 && MEM_W(0xA4, camera) == 0);
        assert(uint32_t(MEM_W(0xA8, camera)) == bits(4.0f));
        assert(uint32_t(MEM_W(0x8C, camera)) == bits(2.0f));
        // Entry uses a0; exit must not depend on argument registers surviving.
        ctx.r4 = 0;
        wdc::cheats::set_enabled(Cheat::TopDown, false);
        MEM_W(0, state) = int32_t(0x8004E950);
        wdc_cheat_top_down_end(rdram, &ctx);
        assert(uint32_t(MEM_W(0, state)) == 0x8004E950);
        MEM_W(0, state) = int32_t(0x8000C560);
        assert(memory == baseline);
    }

    wdc::cheats::set_enabled(Cheat::TopDown, true);
    // The translated function is nonrecursive. Consecutive updates of either
    // local camera must leave no state for the next call.
    const auto baseline = memory;
    for (auto scene : scenes) {
        ctx.r4 = scene + 0x11C;
        wdc_cheat_top_down_begin(rdram, &ctx);
        wdc_cheat_top_down_end(rdram, &ctx);
        assert(memory == baseline);
    }
    wdc_cheat_top_down_end(rdram, &ctx); // Unmatched exit is harmless.
    assert(memory == baseline);

    // A missing epilogue must not make the next invocation snapshot already
    // overridden fields and permanently lose the original camera settings.
    ctx.r4 = scenes[0] + 0x11C;
    wdc_cheat_top_down_begin(rdram, &ctx);
    wdc_cheat_top_down_begin(rdram, &ctx);
    wdc_cheat_top_down_end(rdram, &ctx);
    assert(memory == baseline);

    for (const uint64_t camera : {uint64_t(0), uint64_t(0x800ABF5D),
             uint64_t(0x807FFFFC), uint64_t(0x800ABF60), uint64_t(0x1800ABF5C)}) {
        ctx.r4 = camera;
        wdc_cheat_top_down_begin(rdram, &ctx);
        wdc_cheat_top_down_end(rdram, &ctx);
        assert(memory == baseline);
    }
    ctx.r4 = scenes[0] + 0x11C;
    for (uint32_t scale : {0u, 0x80000000u, bits(-1.0f), 0x7F800000u,
             0x7FC12345u, 0x7F7FFFFFu}) {
        MEM_W(0x8C, int32_t(ctx.r4)) = scale;
        const auto unchanged = memory;
        wdc_cheat_top_down_begin(rdram, &ctx);
        wdc_cheat_top_down_end(rdram, &ctx);
        assert(memory == unchanged);
    }
    MEM_W(0x8C, int32_t(ctx.r4)) = bits(2.0f);
    MEM_W(0, state) = int32_t(0x8004E950); // Menu uses the same scene storage.
    const auto menu = memory;
    wdc_cheat_top_down_begin(rdram, &ctx);
    wdc_cheat_top_down_end(rdram, &ctx);
    assert(memory == menu);
    MEM_W(0, state) = int32_t(0x8000C560);

    // Restoration needs the saved camera, not the caller's argument context.
    ctx.r4 = scenes[0] + 0x11C;
    const auto before_exit = memory;
    wdc_cheat_top_down_begin(rdram, &ctx);
    wdc_cheat_top_down_end(rdram, nullptr);
    assert(memory == before_exit);

    // An invalid/mismatched allocation discards the snapshot without writing
    // through a retained pointer. A later exit must not restore an orphan.
    std::vector<uint8_t> other(memory.size(), 0xA5);
    const auto other_before = other;
    for (auto* invalid_owner : {other.data(), static_cast<uint8_t*>(nullptr)}) {
        wdc_cheat_top_down_begin(rdram, &ctx);
        const auto substituted = memory;
        wdc_cheat_top_down_end(invalid_owner, &ctx);
        assert(memory == substituted && other == other_before);
        wdc_cheat_top_down_end(rdram, &ctx);
        assert(memory == substituted);
        // Model reinitialization after losing the original allocation/context.
        std::copy(before_exit.begin(), before_exit.end(), memory.begin());
        wdc_cheat_top_down_begin(rdram, &ctx);
        wdc_cheat_top_down_end(rdram, &ctx);
        assert(memory == before_exit);
    }
    wdc_cheat_top_down_begin(nullptr, &ctx);
    wdc_cheat_top_down_begin(rdram, nullptr);
    wdc_cheat_top_down_end(rdram, &ctx);
    assert(memory == before_exit);

    constexpr uint64_t native = 0xFEDCBA9876543210ull;
    wdc::cheats::set_enabled(Cheat::Cinematic, true);
    for (auto scene : scenes) {
        MEM_W(0x130, int32_t(ctx.r29)) = scene;
        const auto unchanged = memory;
        assert(wdc_cheat_cinematic(rdram, &ctx, native) == 2);
        assert(memory == unchanged);
        wdc::cheats::set_enabled(Cheat::Cinematic, false);
        assert(wdc_cheat_cinematic(rdram, &ctx, native) == native);
        wdc::cheats::set_enabled(Cheat::Cinematic, true);
    }
    MEM_W(0x130, int32_t(ctx.r29)) = 0x800ABE44;
    assert(wdc_cheat_cinematic(rdram, &ctx, native) == native);
    MEM_W(0x130, int32_t(ctx.r29)) = scenes[0];
    MEM_W(0, state) = int32_t(0x8004E950);
    assert(wdc_cheat_cinematic(rdram, &ctx, native) == native);
    MEM_W(0, state) = int32_t(0x8000C560);
    for (const uint64_t stack : {uint64_t(0), uint64_t(0x807FFFFC),
             uint64_t(0x807FF001), uint64_t(0x1807FF000)}) {
        ctx.r29 = stack;
        assert(wdc_cheat_cinematic(rdram, &ctx, native) == native);
    }
    assert(wdc_cheat_cinematic(nullptr, &ctx, native) == native);
    assert(wdc_cheat_cinematic(rdram, nullptr, native) == native);
    wdc::cheats::set_enabled(Cheat::TopDown, false);
    wdc::cheats::set_enabled(Cheat::Cinematic, false);
}
