#include "game_visibility.h"
#include "recomp.h"
#include "guest_memory.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>

namespace {
std::atomic<unsigned> policy{1};
unsigned settings() { return policy.load(std::memory_order_relaxed); }
float distance_scale() { return float(settings() & 7); }
bool node_valid(uint32_t address) {
    return address >= 0x80000400u && address <= 0x80800000u - 0x24 && !(address & 3);
}
}
void wdc::visibility::configure(bool full_cars, bool full_track, int distance) {
    policy.store(unsigned(std::clamp(distance, 1, 4)) |
        (full_cars ? 8u : 0u) | (full_track ? 16u : 0u), std::memory_order_relaxed);
}

// USA func_800108B4, before 800108D0: t7 is the car-list loop bound.
// The menu keeps three preview cars staged outside the original 4:3 camera;
// widening that camera exposes their shadows (and bodies at ultrawide).
// Title (44) and main menu (0) have no car preview. Suppress their draw loop
// explicitly, retaining the ROM's list finalization and all simulation updates.
// Game render thread, before BOSS submission. Read-only, game-owned u32 guest
// words in word-swapped RDRAM: 8007D848 is the render callback dispatched by
// func_80001D88 (set by func_80001D70); 8007E570 is the active scene; 80094810
// is menu 80092EE8 + 1928, the current page returned by func_800525EC.
// Only menu renderer 8004E950's primary scene 800ABE40 uses this policy.
// That scene storage is reused by races, so the callback guard is essential.
// Other renderers/scenes/pages
// (including transitions) retain the ROM count, so stale menu state is harmless.
// Fixed addresses are inside 8 MiB RDRAM; no pointers are dereferenced/retained.
// Remove when the guest menu owns explicit preview visibility.
extern "C" void wdc_menu_car_visibility(uint8_t* rdram, recomp_context* ctx) {
    if (uint32_t(MEM_W(0, int32_t(0x8007D848))) != 0x8004E950u) return;
    if (uint32_t(MEM_W(0, int32_t(0x8007E570))) != 0x800ABE40u) return;
    const int32_t page = MEM_W(0, int32_t(0x80094810));
    if (page == 0 || page == 44) ctx->r15 = 0;
}

// USA game-thread register hooks. Float32 distances use the ROM's world units;
// perspective multiplies them by 32 downstream. No guest pointers or state are
// changed here. Each operand is freshly produced by the ROM, so disabling the
// option immediately restores original behavior without cumulative scaling.
extern "C" void wdc_projection_distance(uint8_t*, recomp_context* ctx) {
    // 80011DE8: f10 is scene far distance, before multiplication by 32.
    ctx->f10.fl *= distance_scale();
}
// USA 80011DFC, after guPerspectiveF: BOSS uses perspNorm both for reciprocal
// W and as a projected-coordinate clamp. A larger far plane reduces it (Hawaii
// 10 -> 2 at 4x), clipping sky vertices before their texture planes are built.
// Keep the ROM normalization computed from the original distance, while retaining
// the extended projection matrix. This function's stack +0x34 holds that original
// float32 world distance; near is 32 and far is distance * 32 in guPerspectiveF.
// Game render thread, runtime-owned word-swapped RDRAM: read four aligned stack
// bytes and write the ROM's u16 normalization at 8007ED64. No pointer is retained.
// Invalid stack ranges abort; invalid distance leaves the ROM result untouched.
// Remove when the guest projection builder separates range from normalization.
extern "C" void wdc_projection_normalize(uint8_t* rdram, recomp_context* ctx) {
    wdc_require_guest_range(ctx->r29 + 0x34, 4, 4);
    const float distance = std::bit_cast<float>(uint32_t(MEM_W(0x34, ctx->r29)));
    if (!std::isfinite(distance) || distance < 0) return;
    const float sum = 32.0f + distance * 32.0f;
    const auto norm = uint16_t(std::max(1.0, std::min(65535.0, 131072.0 / double(sum))));
    MEM_H(0, int32_t(0x8007ED64)) = norm;
}
extern "C" void wdc_car_distance(uint8_t*, recomp_context* ctx) {
    // 800219F0: f8 is far threshold, f0 remains actual camera depth.
    ctx->f8.fl *= distance_scale();
}
extern "C" void wdc_car_detail(uint8_t*, recomp_context* ctx) {
    // 80021BE8: compare the next detail threshold against projected size.
    // Only the comparison operand changes; car+9B0 (size/fade) is preserved.
    if (settings() & 8) ctx->f6.fl = std::numeric_limits<float>::infinity();
}
extern "C" void wdc_track_detail(uint8_t*, recomp_context* ctx) {
    // 80023244: v0 is the detail index AFTER multiplayer/mode remapping.
    if (settings() & 16) ctx->r2 = 0;
}

// USA 80023674, before the ROM builds its visibility list. s1 is the current
// track section; a3/t0 are forward/backward section counts (current excluded).
// Nodes are game-owned for this render call, with big-endian u32 next/previous
// links at +0/+4 accessed through word-swapped RDRAM helpers. Read only on the
// guest thread; never retain a pointer. 800B2880..800B297F holds 64 pointers;
// the matrix scratch area starts at 800B2980. Validate the original walk before
// extending it, avoid duplicate/invalid links, and never exceed that capacity.
// Invalid original input leaves registers untouched; a bad extension stops at
// the last valid node. Replace this bridge with a named guest visibility API
// when the track loader/renderer is decompiled.
extern "C" void wdc_track_visibility(uint8_t* rdram, recomp_context* ctx) {
    const int scale = int(settings() & 7);
    if (scale == 1) return;
    const int ahead = int32_t(ctx->r7), behind = int32_t(ctx->r8);
    if (ahead < 0 || behind < 0 || ahead > 63 || behind > 63 || ahead + behind >= 64) return;
    const uint32_t start = uint32_t(ctx->r17);
    if (!node_valid(start)) return;
    std::array<uint32_t, 64> seen{};
    int count = 1;
    seen[0] = start;
    auto step = [&](uint32_t& node, int offset) {
        const uint32_t next = uint32_t(MEM_W(offset, int32_t(node)));
        if (!node_valid(next) || count == 64 ||
            std::find(seen.begin(), seen.begin() + count, next) != seen.begin() + count) return false;
        seen[count++] = next;
        node = next;
        return true;
    };
    uint32_t front = start, back = start;
    for (int i = 0; i < ahead; ++i) if (!step(front, 0)) return;
    for (int i = 0; i < behind; ++i) if (!step(back, 4)) return;
    int forward = ahead, backward = behind;
    bool more_front = true, more_back = true;
    // Alternate extensions so a long forward range cannot consume all slots.
    while (count < 64 && (more_front || more_back)) {
        more_front = more_front && forward < ahead * scale && step(front, 0);
        if (more_front) ++forward;
        more_back = more_back && backward < behind * scale && step(back, 4);
        if (more_back) ++backward;
    }
    ctx->r7 = forward;
    ctx->r8 = backward;
}
