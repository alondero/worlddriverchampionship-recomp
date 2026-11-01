#include "game_cheats.h"
#include "recomp.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr uint32_t state_slot = 0x8007D848;
constexpr uint32_t race_state = 0x8000C560;
constexpr std::array<uint32_t, 2> scenes{0x800ABE40, 0x800AE670};
constexpr std::array<uint32_t, 5> offsets{0x94, 0x9C, 0xA0, 0xA4, 0xA8};

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
uint32_t float_word(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
struct CameraFrame {
    uint8_t* owner = nullptr;
    uint32_t camera = 0;
    std::array<uint32_t, offsets.size()> original{};
};
// func_80012354 is nonrecursive. A single snapshot needs no heap allocation.
thread_local CameraFrame active_frame;

void restore_frame(uint8_t* rdram) {
    if (!active_frame.owner) return;
    const auto frame = active_frame;
    active_frame = {}; // Every exit consumes the snapshot, including bad exits.
    if (frame.owner != rdram) return;
    const gpr camera = int32_t(frame.camera);
    for (std::size_t i = 0; i < offsets.size(); ++i)
        MEM_W(offsets[i], camera) = frame.original[i];
}
}

// USA func_80012354 entry (a0) through its sole epilogue at 80012870.
// The two local scene objects at 800ABE40/800AE670 own embedded cameras +11C.
// Their +8C is float32 distance scale, +94/+9C are scaled distance/height in
// world units, and +A0/+A4/+A8 are a float32 target offset vector. MEM_W handles
// the runtime's word-swapped representation of these N64 big-endian words.
// Only active races (u32 callback at 8007D848 == 8000C560) use this override.
// Snapshot five words on the calling game thread, substitute the original
// button handler's 13*scale, 11*scale and (0,0,4), then restore every bit at
// epilogue regardless of later UI toggles or state changes. The translated
// function reads but does not write these five fields. No save object changes.
// A snapshot's address/RDRAM identity lasts until exit. A missing epilogue is
// recovered before the next enabled entry if it supplies the same RDRAM.
// Bad inputs or nonpositive/nonfinite scales never create a new snapshot.
// An exit with null/mismatched RDRAM discards it without writing either allocation;
// it cannot safely restore fields after losing the owner's identity. Exit does
// not need ctx and still restores when the option is switched off mid-call.
// Remove these bridges when the guest camera exposes scoped view parameters.
extern "C" void wdc_cheat_top_down_begin(uint8_t* rdram, recomp_context* ctx) {
    if (!wdc::cheats::enabled(wdc::cheats::Cheat::TopDown)) return;
    restore_frame(rdram);
    if (!rdram || !ctx || !word_range(ctx->r4, 0xAC)) return;
    const auto camera = uint32_t(ctx->r4);
    if (!local_scene(camera - 0x11C) ||
        uint32_t(MEM_W(0, int32_t(state_slot))) != race_state) return;

    const uint32_t scale_bits = uint32_t(MEM_W(0x8C, int32_t(camera)));
    float scale;
    std::memcpy(&scale, &scale_bits, sizeof(scale));
    const float distance = 13.0f * scale, height = 11.0f * scale;
    if (!(scale > 0.0f) || !std::isfinite(scale) ||
        !std::isfinite(distance) || !std::isfinite(height)) return;
    const std::array<uint32_t, 5> replacement{
        float_word(distance), float_word(height), 0, 0, float_word(4.0f)};
    active_frame.owner = rdram;
    active_frame.camera = camera;
    const gpr camera_base = int32_t(camera);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        active_frame.original[i] = uint32_t(MEM_W(offsets[i], camera_base));
        MEM_W(offsets[i], camera_base) = replacement[i];
    }
}

extern "C" void wdc_cheat_top_down_end(uint8_t* rdram, recomp_context*) {
    restore_frame(rdram);
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
