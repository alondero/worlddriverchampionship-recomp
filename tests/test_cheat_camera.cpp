#undef NDEBUG
#include "game_cheats.h"
#include "recomp.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <vector>

extern "C" {
int wdc_camera_top_down(uint8_t*, uint64_t, uint32_t*, uint32_t*);
uint64_t wdc_cheat_cinematic(uint8_t*, recomp_context*, uint64_t);
}

namespace {
constexpr int32_t state = int32_t(0x8007D848);
constexpr std::array<uint32_t, 2> scenes{0x800ABE40, 0x800AE670};
constexpr std::array<int32_t, 5> offsets{0x94, 0x9C, 0xA0, 0xA4, 0xA8};
uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }
}

int main() {
    using wdc::cheats::Cheat;
    std::vector<uint8_t> memory(8 * 1024 * 1024, 0x5A);
    auto* rdram = memory.data();
    MEM_W(0, state) = int32_t(0x8000C560);
    recomp_context ctx{};
    ctx.r29 = int32_t(0x807FF000);
    std::array<uint32_t, 2> effective{};
    const auto sample = [&](uint64_t camera) {
        return wdc_camera_top_down(rdram, camera, &effective[0], &effective[1]);
    };
    for (const auto scene : scenes) {
        const uint32_t camera = scene + 0x11C;
        MEM_W(0x8C, int32_t(camera)) = bits(2.0f);
        const std::array<uint32_t, 5> authored{
            bits(3.0f), bits(5.0f), 0x7FC12345, 0x80000000, bits(9.0f)};
        for (std::size_t i = 0; i < offsets.size(); ++i)
            MEM_W(offsets[i], int32_t(camera)) = authored[i];
        const auto baseline = memory;

        wdc::cheats::set_enabled(Cheat::TopDown, false);
        assert(!sample(camera) && memory == baseline);
        wdc::cheats::set_enabled(Cheat::TopDown, true);
        assert(sample(camera) && sample(int32_t(camera)));
        assert(effective[0] == bits(26.0f) && effective[1] == bits(22.0f));
        assert(memory == baseline);
    }

    const uint32_t camera = scenes[0] + 0x11C;
    const auto baseline = memory;
    const auto valid = effective;
    for (const uint64_t address : {uint64_t(0), uint64_t(0x800ABF5D),
             uint64_t(0x807FFFFC), uint64_t(0x800ABF60), uint64_t(0x1800ABF5C)}) {
        assert(!sample(address) && memory == baseline && effective == valid);
    }
    assert(!wdc_camera_top_down(nullptr, camera, &effective[0], &effective[1]));
    for (uint32_t scale : {0u, 0x80000000u, bits(-1.0f), 0x7F800000u,
             0x7FC12345u, 0x7F7FFFFFu}) {
        MEM_W(0x8C, int32_t(camera)) = scale;
        const auto unchanged = memory;
        assert(!sample(camera) && memory == unchanged && effective == valid);
    }
    MEM_W(0x8C, int32_t(camera)) = bits(2.0f);
    MEM_W(0, state) = int32_t(0x8004E950); // Menu uses the same scene storage.
    const auto menu = memory;
    assert(!sample(camera) && memory == menu && effective == valid);
    MEM_W(0, state) = int32_t(0x8000C560);

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
