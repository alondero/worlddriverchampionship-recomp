#include "game_hud.h"
#include "guest_memory.h"
#include "recomp.h"
#include <atomic>
#include <cstring>

namespace {
std::atomic<int> placement{1};
// Game-thread state, used only while func_8002E13C builds its HUD packets.
bool active = false;
uint32_t left = 0, right = 0;

// USA frame arena: 0x8007ED84 is the ascending BOSS command cursor;
// 0x8007FE74 is the descending packet cursor used by func_8002D198.
// Both are u32 KSEG0 pointers into runtime-owned word-swapped RDRAM. Reserve
// a complete 64-byte BOSS DMA block plus one 8-byte link, on the game thread
// before task submission. Validate both ends and collision before any write.
// Lifetime is the ROM's frame arena; no host pointers survive submission.
// Invalid cursors abort, like the existing native guest bridges; a full arena
// returns false with nothing written. `spare` keeps room for that many later
// packets. Replace this adapter when a guest HUD implementation can emit
// explicit alignment itself.
bool emit(uint8_t* rdram, uint32_t payload, unsigned spare = 0) {
    const uint32_t cursor = MEM_W(0, int32_t(0x8007ED84));
    const uint32_t top = MEM_W(0, int32_t(0x8007FE74));
    wdc_require_guest_range(cursor, 8, 8);
    wdc_require_guest_range(top - 64, 64, 8);
    if (uint64_t(cursor) + 8 * (spare + 1) >= uint64_t(top) - 64 * (spare + 1)) return false;
    const gpr packet = int32_t(top - 64);
    std::memset(rdram + (uint32_t(packet) & 0x7FFFFF), 0, 64);
    MEM_W(0, packet) = wdc::hud::marker;
    MEM_W(4, packet) = payload;
    MEM_W(8, packet) = 0xDF000000; // BOSS raw-list terminator, not sent to RDP.
    MEM_W(0, int32_t(cursor)) = 0x0E000000;
    MEM_W(4, int32_t(cursor)) = uint32_t(packet) & 0xFFFFFF;
    MEM_W(0, int32_t(0x8007FE74)) = uint32_t(packet);
    MEM_W(0, int32_t(0x8007ED84)) = cursor + 8;
    return true;
}
void require(bool emitted) {
    if (emitted) return;
    std::fprintf(stderr, "[wdc] HUD marker exhausted frame arena\n");
    std::abort();
}
bool filling = false;
}
namespace wdc::hud {
void set_placement(int value) { placement = value; }
float needle_shift(float x, float width, float scale) {
    if (!active) return 0.0f;
    const float anchor = x < (left + right) / 2.0f ? float(left) : float(right);
    return edge_shift(anchor, width, scale, placement.load());
}
void active_picture_size(uint8_t* rdram, int32_t& logical_h, int32_t& buffer_h) {
    logical_h = 0;
    buffer_h = 0;
    if (!rdram) return;
    const uint32_t pointer = uint32_t(MEM_W(0, int32_t(0x8007D75C)));
    if (pointer < 0x80000000u || pointer - 0x80000000u > 0x800000u - 16 || (pointer & 3))
        return;
    logical_h = MEM_W(4, int32_t(pointer));
    buffer_h = MEM_W(12, int32_t(pointer));
}
}

// The active scene pointer at 0x8007E570 is a ROM-owned u32. Its first four
// s32 fields are viewport x/y/width/height in framebuffer pixels. Read only on
// the game-thread HUD boundary; validate before use, retain values not pointers.
extern "C" void wdc_hud_begin(uint8_t* rdram, recomp_context*) {
    const gpr scene = MEM_W(0, int32_t(0x8007E570));
    wdc_require_guest_range(scene, 16, 4);
    const int32_t x = MEM_W(0, scene), width = MEM_W(8, scene);
    if (x < 0 || width <= 0 || x + int64_t(width) > 4096) {
        std::fprintf(stderr, "[wdc] invalid HUD viewport\n");
        std::abort();
    }
    left = x; right = x + width;
    require(emit(rdram, (left << 16) | right));
    active = true;
}
extern "C" void wdc_hud_end(uint8_t* rdram, recomp_context*) {
    if (active) require(emit(rdram, wdc::hud::end_marker));
    active = false;
}

// USA menu frame art: func_800500E0's band blit (0x80050104) and
// func_8005063C's separator-bar blit (0x8005065C), each bracketed on the game
// thread around one sprite call. Overlays the band builder draws afterwards
// stay outside. The renderer extends the bracketed texrects' outer columns
// to the output edges. Cosmetic: a full arena skips the bracket entirely.
extern "C" void wdc_menu_fill_begin(uint8_t* rdram, recomp_context*) {
    filling = emit(rdram, wdc::hud::edge_fill, 1);
}
extern "C" void wdc_menu_fill_end(uint8_t* rdram, recomp_context*) {
    // Begin reserved this packet; the blit's own use of the arena could still
    // exhaust it, and an open bracket would extend unrelated menu art.
    if (filling) require(emit(rdram, wdc::hud::end_marker));
    filling = false;
}
