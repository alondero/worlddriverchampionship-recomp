#include "game_warp.h"
#include "recomp.h"
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>

extern "C" {
void func_80068AA8(uint8_t*, recomp_context*);
void func_8000A200(uint8_t*, recomp_context*);
void func_8000A218(uint8_t*, recomp_context*);
}

namespace {
// Publish pending + repeat together: SDL and scripted VI requests may race.
// Low word is pending; high word retains the last requested setup.
std::atomic<uint64_t> requests{};
unsigned take_request() {
    return unsigned(requests.fetch_and(0xFFFFFFFF00000000ULL, std::memory_order_relaxed));
}
// Initialized before guest/VI threads start; read-only afterward.
unsigned scheduled_vi = 0, scheduled_request = 0;
// Only the render/game dispatcher hooks below own these variables.
unsigned loading = 0;
int building_laps = 0;
bool awaiting_race = false;
constexpr int32_t config = int32_t(0x80092360);
constexpr int32_t career = int32_t(0x80097D00);
bool valid(wdc::warp::Request r) {
    using wdc::warp::Mode;
    return r.track >= 1 && r.track <= 58 && r.car >= 0 && r.car <= 34 &&
        (r.mode == Mode::Practice || r.mode == Mode::TimeAttack || r.mode == Mode::Arcade) &&
        (r.laps == 0 || (r.mode == Mode::Arcade && r.laps >= 2 && r.laps <= 5));
}
unsigned pack(wdc::warp::Request r) {
    return unsigned(r.track) | (unsigned(r.car) << 6) | (unsigned(r.mode) << 12) | (unsigned(r.laps) << 16);
}
wdc::warp::Request unpack(unsigned r) {
    return {int(r & 63), int((r >> 6) & 63), wdc::warp::Mode((r >> 12) & 15), int((r >> 16) & 7)};
}
bool number(std::string_view s, int& value) {
    if (s.empty() || s.front() < '0' || s.front() > '9') return false;
    auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    return error == std::errc{} && end == s.data() + s.size();
}
}

bool wdc::warp::parse(std::string_view text, Request& result) {
    std::string_view parts[4];
    int count = 0;
    for (;;) {
        if (count == 4) return false;
        const auto colon = text.find(':');
        parts[count++] = text.substr(0, colon);
        if (parts[count - 1].empty()) return false;
        if (colon == std::string_view::npos) break;
        text.remove_prefix(colon + 1);
    }
    Request r;
    if (!number(parts[0], r.track) || (count > 1 && !number(parts[1], r.car))) return false;
    if (count > 2) {
        if (parts[2] == "practice") r.mode = Mode::Practice;
        else if (parts[2] == "arcade") r.mode = Mode::Arcade;
        else if (parts[2] == "time-attack") r.mode = Mode::TimeAttack;
        else return false;
    }
    if ((count > 3 && !number(parts[3], r.laps)) || !valid(r)) return false;
    result = r;
    return true;
}
bool wdc::warp::request(Request r) {
    if (!valid(r)) return false;
    const auto value = pack(r);
    requests.store((uint64_t(value) << 32) | value, std::memory_order_relaxed);
    return true;
}
void wdc::warp::repeat() {
    auto state = requests.load(std::memory_order_relaxed);
    while (state >> 32) {
        const auto next = (state & 0xFFFFFFFF00000000ULL) | (state >> 32);
        if (requests.compare_exchange_weak(state, next, std::memory_order_relaxed)) break;
    }
}
bool wdc::warp::configure_from_environment() {
    if (const char* at = std::getenv("WDC_WARP_AT")) {
        const std::string_view text(at);
        const auto colon = text.find(':');
        int vi = 0;
        Request r;
        if (colon == std::string_view::npos || !number(text.substr(0, colon), vi) || vi < 1 ||
            !parse(text.substr(colon + 1), r)) {
            std::fprintf(stderr, "[wdc:warp] invalid WDC_WARP_AT; expected vi:track[:car[:mode[:laps]]], vi 1..2147483647\n");
            return false;
        }
        scheduled_vi = unsigned(vi);
        scheduled_request = pack(r);
    }
    const char* value = std::getenv("WDC_WARP");
    if (!value) return true;
    Request r;
    if (!parse(value, r)) {
        std::fprintf(stderr, "[wdc:warp] invalid WDC_WARP; expected track[:car[:practice|arcade|time-attack[:laps]]], track 1..58, car 0..34, arcade laps 2..5 (0 = default)\n");
        return false;
    }
    return request(r);
}
void wdc::warp::vi_tick(unsigned vi) {
    if (scheduled_vi && vi == scheduled_vi) request(unpack(scheduled_request));
}

// USA guest-memory bridge, render/game dispatcher thread only, before the
// menu consumes its exit flag (8004EAB8) or a race frame starts (8000C560).
// All addresses are fixed within 8 MiB RDRAM, N64 big-endian via MEM_W/B.
// 8007D848: u32 active render callback; 8007DF44: u32 fade-out request.
// 80092EBC: u8 menu-active flag, cleared by the native menu exit path.
// No SDL calls or guest pointers cross threads: publishers only write atomics.
// Requests wait through boot, loaders and fades; never replace a running
// callback or bypass its native graphics/audio cleanup. Bad/null inputs do
// nothing. Replace address hooks with named scene APIs when decompiled.
extern "C" void wdc_warp_menu(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || loading || MEM_W(0, int32_t(0x8007D848)) != int32_t(0x8004E950)) return;
    loading = take_request();
    if (!loading) return;
    // The cleanup reads the mode to choose its return menu. The loader hook
    // builds the rest after cleanup, so the current scene retains its config.
    MEM_W(0x84, config) = int(unpack(loading).mode);
    MEM_B(0, int32_t(0x80092EBC)) = 0;
}
extern "C" void wdc_warp_race(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || MEM_W(0, int32_t(0x8007D848)) != int32_t(0x8000C560)) return;
    if (awaiting_race) {
        std::fprintf(stderr, "[wdc:warp] race ready: car=%d mode=%d laps=%d\n",
            MEM_W(config, 8), MEM_W(config, 0x84), MEM_W(config, 0x80));
        awaiting_race = false;
    }
    if (loading || MEM_W(0, int32_t(0x8007DF44))) return;
    loading = take_request();
    if (!loading) return;
    auto call = *ctx;
    call.r4 = int32_t(0x800369E0);
    func_8000A200(rdram, &call);
    func_8000A218(rdram, &call);
}

// Loader entry, after the old scene's cleanup and before any config reads.
// 80092360: 0xA0-byte race config; 80099B54: same-size Quick Race restart
// snapshot. u32 fields/ids, except float words filled by the original setup.
// The original func_80068AA8 constructs the car grid, track metadata, AI
// profile and cached config. Its career inputs at 80097D00 +1054/+1068/+106C
// (u32 rank/course/model), +10FC/+1075 (u8 mirror/ghost) are borrowed for the
// call and restored before returning, including rank and one-shot mirror.
// No unlock flag or save buffer is changed. Transmission is automatic (0).
// Loading is owned until this hook completes; requests arriving meanwhile
// remain pending. Native constructor uses a copy of the live guest context,
// preserving every register and the valid guest stack. No memory is retained.
extern "C" void wdc_warp_load(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx || !loading) return;
    const auto r = unpack(loading);
    const int32_t offsets[] = {0x1054, 0x1068, 0x106C};
    int32_t saved[3];
    for (int i = 0; i < 3; ++i) saved[i] = MEM_W(offsets[i], career);
    const auto mirror = MEM_BU(career, 0x10FC), ghost = MEM_BU(career, 0x1075);
    MEM_W(0x1068, career) = r.track - 1;
    MEM_W(0x106C, career) = r.car;
    MEM_B(0x10FC, career) = 0;
    MEM_B(0x1075, career) = 0;
    for (int offset = 0; offset < 0xA0; offset += 4) MEM_W(offset, config) = 0;
    MEM_W(0x84, config) = int(r.mode);
    auto call = *ctx;
    call.r4 = career;
    building_laps = r.laps;
    func_80068AA8(rdram, &call);
    building_laps = 0;
    for (int i = 0; i < 3; ++i) MEM_W(offsets[i], career) = saved[i];
    MEM_B(0x10FC, career) = mirror;
    MEM_B(0x1075, career) = ghost;
    MEM_W(0x28, config) = 0;
    // Keep native retry/time-attack cache consistent with the forced gearbox.
    if (r.mode != wdc::warp::Mode::Practice)
        for (int offset = 0; offset < 0xA0; offset += 4)
            MEM_W(offset, int32_t(0x80099B54)) = MEM_W(config, offset);
    std::fprintf(stderr, "[wdc:warp] loading track=%d car=%d mode=%d laps=%d\n",
        r.track, r.car, int(r.mode), MEM_W(config, 0x80));
    loading = 0;
    awaiting_race = true;
}
// Before 80068B88 stores the course's lap count. Override before the native
// arcade AI profile selection, which itself depends on laps. Register only.
extern "C" void wdc_warp_laps(uint8_t*, recomp_context* ctx) {
    if (ctx && building_laps) ctx->r2 = building_laps;
}

// Boot presentation runs synchronously before the dispatcher. Only pending
// development requests bypass its warning/Pak dialogs and logo delay. Hooks
// branch to existing ROM labels: 8003C270 retains the native Pak scan at
// 8003C27C and connected-port loop; 8003C52C retains input/render cleanup.
// This reads only the host atomic; no guest memory or settings are changed.
extern "C" int wdc_warp_boot_pending() {
    return unsigned(requests.load(std::memory_order_relaxed)) != 0;
}
