#include "game_laps.h"
#include "game_warp.h"
#include "guest_memory.h"
#include "recomp.h"
#include <atomic>

namespace {
std::atomic<int> chosen{};

int arcade_profile_column(int32_t laps) {
    switch (laps) {
    case 1: case 2: return 2;
    case 3: return 3;
    case 4: return 4;
    case 5: case 6: case 7: case 8: case 9: case 10: case 15: case 20: return 5;
    default: return 0;
    }
}
}

void wdc::laps::configure(int quick_race_laps) {
    chosen.store(supported(quick_race_laps) ? quick_race_laps : 0, std::memory_order_relaxed);
}

// USA func_80068AA8, game thread, before 80068B88 stores v0 at config+0x80
// (u32 lap count, N64 big-endian through the following sw). v0 is the course
// count just returned by func_80057024. Mode is the u32 at 800923E4
// (config+0x84), 4-byte aligned inside 8 MiB RDRAM, read here through MEM_W.
// The Quick Race menu or a warp wrote that word before this constructor.
// This call reads it and writes nothing. The game thread owns it for the
// call. Arcade is 3 and Time Attack is 5. A saved count of 0 leaves v0.
// A saved count of 1..10, 15, or 20 replaces v0 for those two modes.
// Practice (8) and every other mode leave v0; Practice stores 0 later in
// this function. Championship uses a different constructor. A null context
// or RDRAM pointer leaves v0. No guest pointer is retained. An explicit
// warp count overwrites v0 after this returns.
extern "C" void wdc_quick_race_laps(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx) return;
    const int laps = chosen.load(std::memory_order_relaxed);
    if (!laps || !rdram) return;
    wdc_require_guest_range(0x800923E4, 4, 4);
    const int mode = MEM_W(0, int32_t(0x800923E4));
    if (mode == int(wdc::warp::Mode::Arcade) || mode == int(wdc::warp::Mode::TimeAttack))
        ctx->r2 = gpr(laps);
}

// USA func_80068AA8, game thread, before 80068DA4 (beq v0, 2). Arcade only:
// Time Attack and Practice branch away before the reload at 80068D98. v0 is
// the u32 lap count reloaded from config+0x80. v1 is already 0 and a1 is 2.
// This replaces v0 with the opponent-profile column the game already uses
// for that length. Memory keeps the chosen count, so the race limit and the
// reward handler still see it. The columns match func_80068398: 1 and 2 use
// the 2-lap halfword at 800962BE, 3 uses 800962C2, 4 uses 800962C6, and
// 5..10, 15, and 20 use 800962CA, each plus model*18. Counts 2, 3, 4, and 5
// stay themselves. Any other count, including 0, 11..14, and 16..19, is left
// for the ROM's empty profile path. Register only. A null context returns.
extern "C" void wdc_quick_race_profile(uint8_t*, recomp_context* ctx) {
    if (!ctx) return;
    const int column = arcade_profile_column(int32_t(ctx->r2));
    if (column) ctx->r2 = gpr(column);
}
