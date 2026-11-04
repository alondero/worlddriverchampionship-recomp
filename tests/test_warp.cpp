#undef NDEBUG
#include "game_warp.h"
#include "recomp.h"
#include <cassert>
#include <cstdlib>
#include <vector>

extern "C" void wdc_warp_menu(uint8_t*, recomp_context*);
extern "C" void wdc_warp_race(uint8_t*, recomp_context*);
extern "C" void wdc_warp_load(uint8_t*, recomp_context*);
extern "C" void func_80068AA8(uint8_t*, recomp_context*) {}
extern "C" void func_8000A200(uint8_t*, recomp_context*) {}
extern "C" void func_8000A218(uint8_t*, recomp_context*) {}

int main() {
    using namespace wdc::warp;
    Request result{58, 34, Mode::Arcade, 5};
    for (auto bad : {"", "0", "59", "1:", "1:-1", "1:35", "1:0:unknown", "1:0:arcade:1",
                     "1:0:practice:2", "1:0:arcade:6", "1:0:arcade:2:3", "1x", " 1", "+1",
                     "99999999999999999", "1:0:arcade:2junk"}) {
        assert(!parse(bad, result));
        assert(result.track == 58 && result.car == 34 && result.laps == 5);
    }
    assert(parse("1", result) && result.track == 1 && result.car == 0 && result.mode == Mode::Practice);
    assert(parse("58:34:arcade:5", result) && result.track == 58 && result.car == 34 && result.laps == 5);
    assert(parse("47:16:time-attack", result) && result.mode == Mode::TimeAttack);
    assert(!request({0}) && !request({59}) && !request({1, 35}));
    assert(!request({1, 0, Mode(7)}) && !request({1, 0, Mode::Practice, 2}));
    auto environment = [](const char* name, const char* value) {
#ifdef _WIN32
        _putenv_s(name, value);
#else
        if (*value) setenv(name, value, 1); else unsetenv(name);
#endif
    };
    environment("WDC_WARP", "");
    for (auto bad : {"0:1", "-1:1", "1:", "1:59", "1:1:35", "2147483648:1", "1junk:1"}) {
        environment("WDC_WARP_AT", bad);
        assert(!configure_from_environment());
    }
    environment("WDC_WARP_AT", "9:58:34:arcade:3");
    assert(configure_from_environment());
    environment("WDC_WARP_AT", "");
    environment("WDC_WARP", "59");
    assert(!configure_from_environment());
    environment("WDC_WARP", "");
    std::vector<uint8_t> memory(8 * 1024 * 1024, 0xA5);
    auto before = memory;
    auto* rdram = memory.data();
    recomp_context ctx{};
    assert(request({58, 34}));
    wdc_warp_menu(nullptr, &ctx); wdc_warp_race(rdram, nullptr); wdc_warp_load(nullptr, nullptr);
    wdc_warp_menu(rdram, &ctx); wdc_warp_race(rdram, &ctx);
    assert(memory == before); // Boot/unrecognised callback keeps request pending.
    MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8000C560);
    MEM_W(0, int32_t(0x8007DF44)) = 1;
    before = memory;
    wdc_warp_race(rdram, &ctx);
    assert(memory == before); // Existing fade is never overwritten.
    // A scheduled request is only published at its specified VI. Observe its
    // mode at the menu handoff; the ROM fixture verifies the complete setup.
    MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8004E950);
    vi_tick(8); vi_tick(9);
    wdc_warp_menu(rdram, &ctx);
    assert(MEM_W(0, int32_t(0x800923E4)) == int(Mode::Arcade));
}
