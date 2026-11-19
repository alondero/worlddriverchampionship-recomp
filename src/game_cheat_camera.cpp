#include "game_cheats.h"
#include "recomp.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace {
constexpr uint32_t state_slot = 0x8007D848;
constexpr uint32_t race_state = 0x8000C560;
constexpr std::array<uint32_t, 2> scenes{0x800ABE40, 0x800AE670};

bool word_range(uint64_t value, uint32_t bytes) {
    const auto address = uint32_t(value);
    // Generated pointers may be zero-extended or MIPS sign-extended.
    if (value != address && value != uint64_t(int64_t(int32_t(address)))) return false;
    return address >= 0x80000000 && bytes <= 0x800000 &&
        address - 0x80000000 <= 0x800000 - bytes && !(address & 3);
}
bool local_scene(uint32_t address) {
    return address == scenes[0] || address == scenes[1];
}
}

// At USA func_80012354 entry the game thread samples the session toggle.
// Read the active u32 callback at 8007D848 and float32 distance scale at
// camera+8C through word-swapped N64 RDRAM. Only aligned local scene cameras
// in 8 MiB KSEG0 RAM (800ABE40/800AE670 +11C) are eligible in callback 8000C560.
// On success, supply float32 world-unit distance/height bits to the update's
// host locals. On failure the output slots are unused and the ROM loads its
// authored parameters. The policy keeps no guest pointer or state.
extern "C" int wdc_camera_top_down(uint8_t* rdram, uint64_t address,
                                    uint32_t* distance_bits, uint32_t* height_bits) {
    if (!wdc::cheats::enabled(wdc::cheats::Cheat::TopDown) ||
        !rdram || !word_range(address, 0xAC)) return 0;
    const auto camera = uint32_t(address);
    if (!local_scene(camera - 0x11C) ||
        uint32_t(MEM_W(0, int32_t(state_slot))) != race_state) return 0;

    const auto scale = std::bit_cast<float>(uint32_t(MEM_W(0x8C, int32_t(camera))));
    const float distance = 13.0f * scale, height = 11.0f * scale;
    if (!(scale > 0.0f) || !std::isfinite(scale) ||
        !std::isfinite(distance) || !std::isfinite(height)) return 0;
    *distance_bits = std::bit_cast<uint32_t>(distance);
    *height_bits = std::bit_cast<uint32_t>(height);
    return 1;
}

// USA func_8000E1F4, immediately after the hooked scene+1400 u32 loads.
// Its current scene pointer is the big-endian word at sp+130 throughout those
// sites. Read only that aligned stack word and the callback above on the game
// thread; require the full stack range to be in 8 MiB KSEG0 RDRAM and one of
// the two local scenes. Mode 2 selects original trackside camera keyframes.
// Override only the destination register, retaining native camera/save state.
// Invalid pointers, non-race callbacks and disabled options preserve original.
extern "C" uint64_t wdc_cheat_cinematic(
    uint8_t* rdram, recomp_context* ctx, uint64_t original) {
    if (!rdram || !ctx || !wdc::cheats::enabled(wdc::cheats::Cheat::Cinematic) ||
        !word_range(ctx->r29, 0x134) ||
        uint32_t(MEM_W(0, int32_t(state_slot))) != race_state) return original;
    const auto scene = uint32_t(MEM_W(0x130, int32_t(ctx->r29)));
    return local_scene(scene) ? uint64_t(2) : original;
}
