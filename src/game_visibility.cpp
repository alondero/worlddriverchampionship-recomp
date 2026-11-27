#include "game_visibility.h"
#include "race_globals.h"
#include "recomp.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <limits>

namespace {
constexpr uint32_t max_depth_sort_buckets = 4096;
constexpr uint32_t depth_sort_tails = 0x807C0000u;
static_assert(depth_sort_tails + max_depth_sort_buckets * sizeof(uint32_t) <= wdc::visibility::track_section_list);

std::atomic<unsigned> policy{1};
unsigned settings() { return policy.load(std::memory_order_relaxed); }
bool node_valid(uint32_t address) {
    return address >= 0x80000400u && address <= 0x80800000u - 0x24 && !(address & 3);
}
}
float wdc::visibility::distance_scale() {
    const unsigned distance = settings() & 0xffu;
    return distance == wdc::visibility::unlimited_draw_distance
        ? wdc::visibility::unlimited_draw_distance_scale : float(distance);
}
void wdc::visibility::configure(bool full_cars, bool full_track, int distance) {
    if (distance != wdc::visibility::unlimited_draw_distance) distance = std::clamp(distance, 1, 8);
    policy.store(unsigned(distance) |
        (full_cars ? 0x100u : 0u) | (full_track ? 0x200u : 0u), std::memory_order_relaxed);
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

// USA 800253D4 replaces the primary arena's call to the descending ROM
// allocator on the scene-loading thread, after the prior scene's graphics
// drain. a0 is the arena size in bytes, v0 is its KSEG0 base, and the saved
// u32 at sp+0x18 is the size the ROM publishes after this call. The original
// 4 MiB heap has no bounds check. A larger arena there can push race effects
// into the framebuffers, where RDP writeback corrupts their guest pointers.
// Use a port-owned, 16-byte-aligned slice of the Expansion Pak area, inside
// both BOSS DMA range and the interpolation image. It lasts until scene
// teardown. Original distance and other arena sizes use the ROM allocator.
extern "C" bool wdc_reserve_frame_arena(uint8_t* rdram, recomp_context* ctx) {
    constexpr uint32_t base = 0x80700000u;
    static_assert(base + 0xC0000u <= depth_sort_tails);
    const uint32_t size = uint32_t(ctx->r4);
    if ((settings() & 0xffu) == 1 || (size != 0x40000u && size != 0x60000u)) return false;
    MEM_W(0x18, ctx->r29) = size * 2;
    ctx->r2 = int32_t(base);
    return true;
}

extern "C" void wdc_car_distance(uint8_t*, recomp_context* ctx) {
    // 800219F0: f8 is far threshold, f0 remains actual camera depth.
    ctx->f8.fl *= wdc::visibility::distance_scale();
}
extern "C" void wdc_car_detail(uint8_t*, recomp_context* ctx) {
    // 80021BE8: compare the next detail threshold against projected size.
    // Only the comparison operand changes; car+9B0 (size/fade) is preserved.
    if (settings() & 0x100u) ctx->f6.fl = std::numeric_limits<float>::infinity();
}
extern "C" void wdc_scenery_distance(uint8_t*, recomp_context* ctx) {
    // 80022520: f0 holds the squared camera-to-object distance and f4 the
    // ROM's 384-unit radius squared. Scale only the cutoff, in square world units.
    const float scale = wdc::visibility::distance_scale();
    ctx->f4.fl *= scale * scale;
}
extern "C" void wdc_track_detail(uint8_t*, recomp_context* ctx) {
    // 80023244: v0 is the detail index AFTER multiplayer/mode remapping.
    if (settings() & 0x200u) ctx->r2 = 0;
}

// USA 80010DD4, on the guest render thread before allocating a view's
// sort tables. t0 is the even bucket count, roughly 3 * scene draw distance.
// Read the game-owned, aligned u32 dispatcher callback at 8007D848 through
// word-swapped RDRAM. Only races extend the count. The ROM allocates and
// initializes both tables from this count, then uses it to reject polygons.
// Track polygon keys sum three signed BOSS depths before shifting right by
// five, then subtract an authored bias. 4096 covers the raw 0..3071 keys
// plus up to 1024 buckets of negative bias, with at most 16 KiB per table.
// Counts already at or above the cap keep the ROM's range.
extern "C" void wdc_depth_sort_range(uint8_t* rdram, recomp_context* ctx) {
    namespace race = wdc::render::race;
    const auto buckets = uint32_t(ctx->r8);
    const auto scale = uint32_t(wdc::visibility::distance_scale());
    if (scale == 1 || buckets >= max_depth_sort_buckets ||
        race::word(rdram, race::callback) != race::race_callback) return;
    ctx->r8 = std::min(buckets * scale, max_depth_sort_buckets);
}

// USA 80010E38's allocator call. a0 is the final byte count. sp+0x20
// holds the scene pointer saved at entry. Re-read the same aligned f32
// world distance at scene+0x60 that the ROM just used at 80010DB4.
// Both reads are from game-owned word-swapped RDRAM in the live view.
// Enlarged u32 tail entries are initialized and consumed by the ROM on the
// same render thread before it begins another view. Only the separate head
// table is part of the BOSS command stream. v0 returns the aligned KSEG0
// base within the 8 MiB render image. Unextended sizes use the ROM allocator.
extern "C" bool wdc_reserve_depth_sort_tails(uint8_t* rdram, recomp_context* ctx) {
    const gpr scene = MEM_W(0x20, ctx->r29);
    const float distance = std::bit_cast<float>(uint32_t(MEM_W(0x60, scene)));
    const uint32_t stock_buckets = (uint32_t(TRUNC_W_S(distance)) * 3u) & ~1u;
    const uint32_t buckets = uint32_t(ctx->r4) / sizeof(uint32_t);
    if (buckets <= stock_buckets || buckets > max_depth_sort_buckets) return false;
    ctx->r2 = int32_t(depth_sort_tails);
    return true;
}

// USA 80023674, before the ROM builds its visibility list. s1 is the current
// track section; a3/t0 are forward/backward section counts (current excluded).
// Nodes are game-owned for this render call, with big-endian u32 next/previous
// links at +0/+4 accessed through word-swapped RDRAM helpers. Read only on the
// guest thread; never retain a pointer. Generated list accesses are relocated
// from 800B2880 to 807F2880, where 1024 pointers fit inside the 8 MiB
// interpolation image. Validate the original walk before extending
// it, avoid duplicate/invalid links, and never exceed that bounded capacity.
// Invalid original input leaves registers untouched; a bad extension stops at
// the last valid node. Replace this bridge with a named guest visibility API
// when the track loader/renderer is decompiled.
extern "C" void wdc_track_visibility(uint8_t* rdram, recomp_context* ctx) {
    const int scale = int(settings() & 0xffu);
    if (scale == 1) return;
    constexpr int capacity = wdc::visibility::max_track_sections;
    const int ahead = int32_t(ctx->r7), behind = int32_t(ctx->r8);
    if (ahead < 0 || behind < 0 || ahead >= capacity || behind >= capacity || ahead + behind >= capacity) return;
    const uint32_t start = uint32_t(ctx->r17);
    if (!node_valid(start)) return;
    std::array<uint32_t, capacity> seen{};
    int count = 1;
    seen[0] = start;
    auto step = [&](uint32_t& node, int offset) {
        const uint32_t next = uint32_t(MEM_W(offset, int32_t(node)));
        if (!node_valid(next) || count == capacity ||
            std::find(seen.begin(), seen.begin() + count, next) != seen.begin() + count) return false;
        seen[count++] = next;
        node = next;
        return true;
    };
    uint32_t front = start, back = start;
    for (int i = 0; i < ahead; ++i) if (!step(front, 0)) return;
    for (int i = 0; i < behind; ++i) if (!step(back, 4)) return;
    const bool unlimited = scale == wdc::visibility::unlimited_draw_distance;
    const int forward_target = unlimited ? capacity - 1 : ahead * scale;
    const int backward_target = unlimited ? capacity - 1 : behind * scale;
    int forward = ahead, backward = behind;
    bool more_front = true, more_back = true;
    // Alternate extensions so a long forward range cannot consume all slots.
    while (count < capacity && (more_front || more_back)) {
        more_front = more_front && forward < forward_target && step(front, 0);
        if (more_front) ++forward;
        more_back = more_back && backward < backward_target && step(back, 4);
        if (more_back) ++backward;
    }
    ctx->r7 = forward;
    ctx->r8 = backward;
}
