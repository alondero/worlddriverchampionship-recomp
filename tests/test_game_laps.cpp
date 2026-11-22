#undef NDEBUG
#include "game_laps.h"
#include "game_warp.h"
#include "recomp.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

extern "C" void wdc_warp_menu(uint8_t*, recomp_context*);
extern "C" void wdc_warp_load(uint8_t*, recomp_context*);
extern "C" void wdc_warp_laps(uint8_t*, recomp_context*);
extern "C" void wdc_quick_race_profile(uint8_t*, recomp_context*);

static int32_t hooked = -1;
extern "C" void func_80068AA8(uint8_t* rdram, recomp_context* ctx) {
    wdc_warp_laps(rdram, ctx);
    hooked = int32_t(ctx->r2);
}
extern "C" void func_8000A200(uint8_t*, recomp_context*) {}
extern "C" void func_8000A218(uint8_t*, recomp_context*) {}

namespace {
constexpr int32_t mode_address = int32_t(0x800923E4);

int32_t count_at(uint8_t* rdram, int mode, int incoming) {
    MEM_W(0, mode_address) = mode;
    recomp_context ctx{};
    ctx.r2 = gpr(incoming);
    wdc_warp_laps(rdram, &ctx);
    assert(MEM_W(0, mode_address) == mode);
    return int32_t(ctx.r2);
}

int32_t profile(uint8_t* rdram, int incoming) {
    recomp_context ctx{};
    ctx.r2 = gpr(int32_t(incoming));
    wdc_quick_race_profile(rdram, &ctx);
    return int32_t(ctx.r2);
}

void launch(uint8_t* rdram, wdc::warp::Request request, int course_laps) {
    assert(wdc::warp::request(request));
    recomp_context ctx{};
    ctx.r2 = gpr(course_laps);
    MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8004E950);
    wdc_warp_menu(rdram, &ctx);
    wdc_warp_load(rdram, &ctx);
}
}

int main() {
    using wdc::warp::Mode;
    std::vector<uint8_t> memory(8 * 1024 * 1024, 0);
    auto* rdram = memory.data();
    wdc::laps::configure(0);
    wdc_warp_laps(nullptr, nullptr);
    wdc_quick_race_profile(nullptr, nullptr);

    recomp_context ctx{};
    ctx.r2 = 4;
    wdc_warp_laps(nullptr, &ctx);
    assert(ctx.r2 == 4);

    assert(count_at(rdram, int(Mode::Arcade), 4) == 4);
    assert(count_at(rdram, int(Mode::TimeAttack), 3) == 3);
    for (int laps : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20}) {
        wdc::laps::configure(laps);
        assert(count_at(rdram, int(Mode::Arcade), 4) == laps);
        assert(count_at(rdram, int(Mode::TimeAttack), 2) == laps);
        assert(count_at(rdram, int(Mode::Practice), 5) == 5);
        assert(count_at(rdram, 4, 5) == 5);
    }
    for (int laps : {-1, 0, 11, 14, 16, 19, 21}) {
        wdc::laps::configure(laps);
        assert(count_at(rdram, int(Mode::Arcade), 4) == 4);
        assert(count_at(rdram, int(Mode::TimeAttack), 3) == 3);
    }

    const auto before = memory;
    assert(profile(rdram, 1) == 2);
    assert(profile(rdram, 2) == 2);
    assert(profile(rdram, 3) == 3);
    assert(profile(rdram, 4) == 4);
    assert(profile(rdram, 5) == 5);
    for (int laps : {6, 7, 8, 9, 10, 15, 20}) assert(profile(rdram, laps) == 5);
    for (int laps : {0, 11, 12, 13, 14, 16, 17, 18, 19, 21, -1}) assert(profile(rdram, laps) == laps);
    assert(memory == before);

    wdc::laps::configure(20);
    launch(rdram, {1, 0, Mode::Arcade, 3}, 4);
    assert(hooked == 3);
    launch(rdram, {1, 0, Mode::Arcade, 0}, 4);
    assert(hooked == 20);
    launch(rdram, {1, 0, Mode::Arcade, 5}, 2);
    assert(hooked == 5);
    launch(rdram, {2, 0, Mode::Practice, 0}, 4);
    assert(hooked == 4);
    wdc::laps::configure(1);
    launch(rdram, {3, 0, Mode::TimeAttack, 0}, 5);
    assert(hooked == 1);
    wdc::laps::configure(10);
    assert(count_at(rdram, int(Mode::Arcade), 4) == 10);

    std::cout << "quick race lap tests passed\n";
    return 0;
}
