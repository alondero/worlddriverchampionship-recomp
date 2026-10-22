#undef NDEBUG
#include "game_hud.h"
#include "recomp.h"
#include <cassert>
#include <vector>

// Runs the translated menu band (func_800500E0) and separator bar
// (func_8005063C) builders with their generated hooks. Only the art blit
// itself may be bracketed for edge fill; overlays drawn by the same builder
// must keep their original placement.
namespace {
constexpr int32_t cursor_slot = int32_t(0x8007ED84), top_slot = int32_t(0x8007FE74);
constexpr int32_t commands = int32_t(0x80300000), packets = int32_t(0x80310000);
struct Blit { uint32_t fn, image, x, y; bool filled; };
std::vector<Blit> blits;
uint32_t overlay_predicate = 0;

uint32_t last_payload(uint8_t* rdram) {
    const gpr top = MEM_W(0, top_slot);
    return top == packets ? 0 : uint32_t(MEM_W(4, top));
}
void record(uint8_t* rdram, recomp_context* ctx, uint32_t fn) {
    blits.push_back({fn, uint32_t(ctx->r4), uint32_t(ctx->r5), uint32_t(ctx->r6),
                     last_payload(rdram) == wdc::hud::edge_fill});
}
void reset(uint8_t* rdram, int32_t top) {
    blits.clear();
    MEM_W(0, cursor_slot) = commands;
    MEM_W(0, top_slot) = top;
}
}
extern "C" {
void func_800500E0(uint8_t*, recomp_context*);
void func_8005063C(uint8_t*, recomp_context*);
void func_8006DA18(uint8_t* rdram, recomp_context* ctx) { record(rdram, ctx, 0x8006DA18); }
void func_8006CCD4(uint8_t* rdram, recomp_context* ctx) { record(rdram, ctx, 0x8006CCD4); }
void func_80051028(uint8_t* rdram, recomp_context* ctx) { record(rdram, ctx, 0x80051028); }
void func_800664F4(uint8_t*, recomp_context* ctx) { ctx->r2 = overlay_predicate; }
void func_80066558(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
}

int main() {
    std::vector<uint8_t> storage(8 * 1024 * 1024);
    auto* rdram = storage.data();
    auto call = [&](void (*fn)(uint8_t*, recomp_context*), int32_t object) {
        recomp_context ctx{};
        ctx.r29 = int32_t(0x807FF000);
        ctx.r4 = object;
        fn(rdram, &ctx);
    };
    constexpr int32_t band = int32_t(0x80094B24), bar = int32_t(0x80094AC4);
    MEM_W(0x58, band) = 5;

    // Band art without overlays: exactly the blit is bracketed, then closed.
    reset(rdram, packets);
    call(func_800500E0, band);
    assert(blits.size() == 1);
    assert(blits[0].fn == 0x8006DA18 && blits[0].image == uint32_t(band));
    assert(blits[0].x == 8 && blits[0].y == 5 && blits[0].filled);
    assert(last_payload(rdram) == wdc::hud::end_marker);
    assert(MEM_W(0, cursor_slot) == commands + 16);
    assert(MEM_W(0, commands) == 0x0E000000 && MEM_W(8, commands) == 0x0E000000);

    // The builder's title overlays follow the art and stay unbracketed.
    overlay_predicate = 1;
    MEM_W(0x74, band) = 0;
    MEM_W(0x78, band) = 1;
    MEM_W(0, int32_t(0x80099AF0)) = 1;
    MEM_W(0, int32_t(0x80099828)) = 0x11;
    reset(rdram, packets);
    call(func_800500E0, band);
    assert(blits.size() == 3);
    assert(blits[0].fn == 0x8006DA18 && blits[0].filled);
    assert(blits[1].fn == 0x8006CCD4 && !blits[1].filled);
    assert(blits[2].fn == 0x80051028 && !blits[2].filled);
    overlay_predicate = 0;

    // Separator bar: its single blit is bracketed.
    MEM_W(4, bar) = 60;
    reset(rdram, packets);
    call(func_8005063C, bar);
    assert(blits.size() == 1);
    assert(blits[0].fn == 0x8006CCD4 && blits[0].image == uint32_t(bar + 0x10));
    assert(blits[0].x == 8 && blits[0].y == 60 && blits[0].filled);
    assert(last_payload(rdram) == wdc::hud::end_marker);

    // A full frame arena skips the cosmetic bracket instead of aborting.
    reset(rdram, commands + 64);
    call(func_8005063C, bar);
    assert(blits.size() == 1 && !blits[0].filled);
    assert(MEM_W(0, cursor_slot) == commands && MEM_W(0, top_slot) == commands + 64);
}
