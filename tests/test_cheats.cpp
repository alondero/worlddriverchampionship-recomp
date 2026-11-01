#undef NDEBUG
#include "game_cheats.h"
#include "recomp.h"
#include <cassert>
#include <cstdint>
#include <vector>

extern "C" {
void wdc_cheat_car_available(uint8_t*, recomp_context*);
void wdc_cheat_car_seen(uint8_t*, recomp_context*);
void wdc_cheat_team_available(uint8_t*, recomp_context*);
void wdc_cheat_track_available(uint8_t*, recomp_context*);
void wdc_cheat_event_available(uint8_t*, recomp_context*);
void wdc_cheat_team_class(uint8_t*, recomp_context*);
void wdc_cheat_restart_count(uint8_t*, recomp_context*);
void wdc_cheat_restart_spend(uint8_t*, recomp_context*);
void wdc_cheat_ending(uint8_t*, recomp_context*);
uint64_t wdc_cheat_pink(uint64_t);
uint64_t wdc_cheat_black(uint64_t);
uint64_t wdc_cheat_small(uint64_t);
uint64_t wdc_cheat_visible(uint64_t);
uint64_t wdc_cheat_red_skids(uint64_t);
uint64_t wdc_cheat_retro(uint64_t);
uint64_t wdc_cheat_blur(uint64_t);
uint64_t wdc_cheat_clear_scene(uint64_t);
uint64_t wdc_cheat_mirror(uint64_t);
uint64_t wdc_cheat_race_select(uint64_t);
uint64_t wdc_cheat_evilrob(uint64_t);
}

namespace {
constexpr int32_t car = int32_t(0x8009884C);  // save+2892, model word at +8
constexpr int32_t team = int32_t(0x80097D2C); // save+44, id word at +0
constexpr int32_t track = int32_t(0x80098B08); // save+3592, lock byte at +5
constexpr uint32_t car_menu[] = {0x80058148, 0x8005A728, 0x8005AED4, 0x8006B9D4};
constexpr uint32_t car_seen_menu[] = {
    0x80058030, 0x800580BC, 0x80059E60, 0x80059FB8, 0x8005A034, 0x8005A0C8, 0x8005A128,
};
constexpr uint32_t team_menu[] = {0x800596CC, 0x80059728, 0x80066584};
constexpr uint32_t track_menu[] = {
    0x80059374, 0x80059404, 0x8005EFBC, 0x8005EFFC,
    0x8005F078, 0x8005F0D4, 0x8005F1DC, 0x8005F23C,
};

int64_t result(void (*hook)(uint8_t*, recomp_context*), uint8_t* rdram, int32_t record, int64_t loaded, uint32_t ra) {
    recomp_context ctx{};
    ctx.r2 = loaded;
    ctx.r4 = record;
    ctx.r31 = ra;
    hook(rdram, &ctx);
    return ctx.r2;
}
}

int main() {
    using wdc::cheats::Cheat;
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    for (const auto& entry : wdc::cheats::catalog) assert(!wdc::cheats::enabled(entry.cheat));
    wdc::cheats::set_enabled(static_cast<Cheat>(99), true);
    assert(!wdc::cheats::enabled(static_cast<Cheat>(99)));

    MEM_W(8, car) = 3;
    MEM_B(0, car) = 0;
    MEM_W(0, team) = 7;
    MEM_B(16, team) = 0;
    MEM_B(5, track) = 0;

    // Off: the loaded lock byte is returned and guest RAM is untouched.
    assert(result(wdc_cheat_car_available, rdram, car, 0, car_menu[0]) == 0);
    assert(result(wdc_cheat_car_seen, rdram, car, 0, car_seen_menu[0]) == 0);
    assert(result(wdc_cheat_team_available, rdram, team, 0, team_menu[0]) == 0);
    assert(result(wdc_cheat_track_available, rdram, track, 0, track_menu[0]) == 0);
    assert(MEM_B(0, car) == 0 && MEM_B(16, team) == 0 && MEM_B(5, track) == 0);

    // An earned unlock stays as the accessor loaded it, even with the cheat on.
    wdc::cheats::set_enabled(Cheat::Gt2Cars, true);
    wdc::cheats::set_enabled(Cheat::Tracks, true);
    assert(result(wdc_cheat_car_available, rdram, car, 1, car_menu[0]) == 1);
    assert(result(wdc_cheat_track_available, rdram, track, 2, track_menu[0]) == 2);

    // GT2 models 0..16 and GT1 models 17..33. The interceptor (34) is neither.
    for (int model = 0; model <= 34; ++model) {
        MEM_W(8, car) = model;
        const bool gt2 = model <= 16;
        const bool gt1 = model >= 17 && model <= 33;
        wdc::cheats::set_enabled(Cheat::Gt2Cars, true);
        wdc::cheats::set_enabled(Cheat::Gt1Cars, false);
        assert(result(wdc_cheat_car_available, rdram, car, 0, car_menu[0]) == (gt2 ? 1 : 0));
        assert(result(wdc_cheat_car_seen, rdram, car, 0, car_seen_menu[0]) == (gt2 ? 1 : 0));
        wdc::cheats::set_enabled(Cheat::Gt2Cars, false);
        wdc::cheats::set_enabled(Cheat::Gt1Cars, true);
        assert(result(wdc_cheat_car_available, rdram, car, 0, car_menu[0]) == (gt1 ? 1 : 0));
        assert(result(wdc_cheat_car_seen, rdram, car, 0, car_seen_menu[0]) == (gt1 ? 1 : 0));
    }
    assert(MEM_B(0, car) == 0);

    // Team id 7 belongs to both button-code ranges. Ids outside 0..14 do not.
    for (int id = -1; id <= 15; ++id) {
        MEM_W(0, team) = id;
        wdc::cheats::set_enabled(Cheat::Gt2Cars, true);
        wdc::cheats::set_enabled(Cheat::Gt1Cars, false);
        const bool gt2 = id >= 0 && id <= 7;
        assert(result(wdc_cheat_team_available, rdram, team, 0, team_menu[0]) == (gt2 ? 1 : 0));
        wdc::cheats::set_enabled(Cheat::Gt2Cars, false);
        wdc::cheats::set_enabled(Cheat::Gt1Cars, true);
        const bool gt1 = id >= 7 && id <= 14;
        assert(result(wdc_cheat_team_available, rdram, team, 0, team_menu[0]) == (gt1 ? 1 : 0));
    }
    assert(MEM_B(16, team) == 0);

    wdc::cheats::set_enabled(Cheat::Tracks, true);
    assert(result(wdc_cheat_track_available, rdram, track, 0, track_menu[0]) == 1);
    wdc::cheats::set_enabled(Cheat::Tracks, false);
    assert(result(wdc_cheat_track_available, rdram, track, 0, track_menu[0]) == 0);
    assert(MEM_B(5, track) == 0);

    // Every measured menu return is overridden. A dropped address fails here.
    wdc::cheats::set_enabled(Cheat::Gt2Cars, true);
    wdc::cheats::set_enabled(Cheat::Gt1Cars, true);
    wdc::cheats::set_enabled(Cheat::Tracks, true);
    MEM_W(8, car) = 3;
    MEM_W(0, team) = 7;
    for (uint32_t ra : car_menu) assert(result(wdc_cheat_car_available, rdram, car, 0, ra) == 1);
    for (uint32_t ra : car_seen_menu) assert(result(wdc_cheat_car_seen, rdram, car, 0, ra) == 1);
    for (uint32_t ra : team_menu) assert(result(wdc_cheat_team_available, rdram, team, 0, ra) == 1);
    for (uint32_t ra : track_menu) assert(result(wdc_cheat_track_available, rdram, track, 0, ra) == 1);

    // A bad pointer or missing context does not invent an unlock.
    wdc::cheats::set_enabled(Cheat::Gt1Cars, true);
    wdc::cheats::set_enabled(Cheat::Gt2Cars, true);
    wdc::cheats::set_enabled(Cheat::Tracks, true);
    assert(result(wdc_cheat_car_available, rdram, 0, 0, car_menu[0]) == 0);
    assert(result(wdc_cheat_car_available, rdram, int32_t(0x80000002), 0, car_menu[0]) == 0);
    assert(result(wdc_cheat_team_available, nullptr, team, 0, team_menu[0]) == 0);
    assert(result(wdc_cheat_track_available, nullptr, track, 0, track_menu[0]) == 0);
    // Save packing and the earned-car writer must keep the real lock byte.
    assert(result(wdc_cheat_car_available, rdram, car, 0, 0x80064B4C) == 0);
    assert(result(wdc_cheat_car_available, rdram, car, 0, 0x80064B90) == 0);
    assert(result(wdc_cheat_car_available, rdram, car, 0, 0x80056A90) == 0);
    assert(result(wdc_cheat_car_seen, rdram, car, 0, 0x80064848) == 0);
    assert(result(wdc_cheat_car_seen, rdram, car, 0, 0x80064890) == 0);
    assert(result(wdc_cheat_car_seen, rdram, car, 0, 0x80056B50) == 0);
    assert(result(wdc_cheat_track_available, rdram, track, 0, 0x800648D8) == 0);
    assert(result(wdc_cheat_track_available, rdram, track, 0, 0x80064924) == 0);
    assert(result(wdc_cheat_track_available, rdram, track, 0, 0) == 0);
    wdc_cheat_car_available(rdram, nullptr);
    wdc_cheat_team_available(rdram, nullptr);
    wdc_cheat_track_available(rdram, nullptr);

    for (const auto& entry : wdc::cheats::catalog) wdc::cheats::set_enabled(entry.cheat, false);

    // Distinct name-password venue ranges, including Black Forest's four
    // layouts. These overrides must not affect any other venue or save read.
    constexpr Cheat venues[] = {Cheat::Hawaii, Cheat::LesGets, Cheat::LasVegas,
        Cheat::NewZealand, Cheat::Lisbon, Cheat::Rome, Cheat::Sydney,
        Cheat::Zurich, Cheat::Kyoto, Cheat::BlackForest};
    for (int venue = 0; venue < 10; ++venue) {
        wdc::cheats::set_enabled(venues[venue], true);
        for (int id = -1; id <= 58; ++id) {
            MEM_W(0, track) = id;
            const bool available = id >= 0 && id < 58 && id / 6 == venue;
            assert(result(wdc_cheat_track_available, rdram, track, 0, track_menu[0]) == available);
            assert(result(wdc_cheat_track_available, rdram, track, 0, 0x800648D8) == 0);
        }
        wdc::cheats::set_enabled(venues[venue], false);
    }

    for (Cheat special : {Cheat::Challenger, Cheat::Interceptor}) {
        wdc::cheats::set_enabled(special, true);
        for (int model = 0; model <= 35; ++model) {
            MEM_W(8, car) = model;
            const bool available = model == (special == Cheat::Challenger ? 16 : 34);
            assert(result(wdc_cheat_car_available, rdram, car, 0, car_menu[0]) == available);
            assert(result(wdc_cheat_car_seen, rdram, car, 0, car_seen_menu[0]) == available);
        }
        wdc::cheats::set_enabled(special, false);
    }
    wdc::cheats::set_enabled(Cheat::Interceptor, true);
    MEM_W(0, team) = 12;
    assert(result(wdc_cheat_team_available, rdram, team, 0, team_menu[0]) == 1);
    assert(result(wdc_cheat_team_class, rdram, team, 0, 0x8005AE80) == 3);
    assert(result(wdc_cheat_team_class, rdram, team, 2, 0x800681FC) == 2);
    wdc::cheats::set_enabled(Cheat::Interceptor, false);

    constexpr int32_t event = int32_t(0x80097F88);
    for (Cheat category : {Cheat::Gt1Events, Cheat::Gt2Events}) {
        wdc::cheats::set_enabled(category, true);
        for (int id = -1; id <= 20; ++id) {
            MEM_W(0x50, event) = id;
            const bool available = category == Cheat::Gt2Events ? id >= 0 && id < 10 : id >= 10 && id < 20;
            assert(result(wdc_cheat_event_available, rdram, event, 0, 0x8005B298) == available);
            assert(result(wdc_cheat_event_available, rdram, event, 0, 0) == 0);
        }
        wdc::cheats::set_enabled(category, false);
    }

    // Register-only effects preserve native cheats and negative strcmp
    // results when off, and return the original immediately after disabling.
    struct Effect { Cheat cheat; uint64_t (*read)(uint64_t); uint64_t on; };
    const Effect effects[] = {
        {Cheat::PinkCars, wdc_cheat_pink, 1}, {Cheat::BlackCars, wdc_cheat_black, 1},
        {Cheat::SmallCars, wdc_cheat_small, 1}, {Cheat::InvisibleCars, wdc_cheat_visible, 0},
        {Cheat::RedSkidmarks, wdc_cheat_red_skids, 1}, {Cheat::RetroGraphics, wdc_cheat_retro, 0},
        {Cheat::ClearScene, wdc_cheat_clear_scene, 1}, {Cheat::MirrorChampionship, wdc_cheat_mirror, 1},
        {Cheat::RaceSelect, wdc_cheat_race_select, 1}, {Cheat::EvilRob, wdc_cheat_evilrob, 0},
    };
    const auto snapshot = memory;
    for (const auto& effect : effects) {
        for (uint64_t original : {uint64_t(0), uint64_t(1), ~uint64_t(0)}) {
            assert(effect.read(original) == original);
            wdc::cheats::set_enabled(effect.cheat, true);
            assert(effect.read(original) == effect.on);
            wdc::cheats::set_enabled(effect.cheat, false);
            assert(effect.read(original) == original);
        }
    }
    wdc::cheats::set_enabled(Cheat::SuperBlur, true);
    assert(wdc_cheat_blur(0) == 0xA0);
    assert(wdc_cheat_blur(0xC0) == 0xC0);
    wdc::cheats::set_enabled(Cheat::SuperBlur, false);
    assert(wdc_cheat_blur(0) == 0);

    wdc::cheats::set_enabled(Cheat::Ending, true);
    assert(result(wdc_cheat_ending, rdram, 0, 0, 0) == 0);
    assert(wdc::cheats::enabled(Cheat::Ending));
    assert(result(wdc_cheat_ending, rdram, 0, 0, 0x800592D4) == 1);
    assert(result(wdc_cheat_ending, rdram, 0, 0, 0x800592D4) == 0);
    assert(!wdc::cheats::enabled(Cheat::Ending));
    assert(memory == snapshot);

    constexpr int32_t career = int32_t(0x80097D00);
    MEM_W(0x1060, career) = 2;
    wdc::cheats::set_enabled(Cheat::InfiniteRestarts, true);
    assert(result(wdc_cheat_restart_count, rdram, career, 2, 0x80045AD4) == 9);
    assert(result(wdc_cheat_restart_count, rdram, career, 2, 0x80064B24) == 2);
    recomp_context ctx{};
    ctx.r4 = career; ctx.r5 = 8; ctx.r31 = int32_t(0x80045AFC);
    wdc_cheat_restart_spend(rdram, &ctx);
    assert(ctx.r5 == 2 && MEM_W(0x1060, career) == 2);
    ctx.r4 = 0; ctx.r5 = 8;
    wdc_cheat_restart_spend(rdram, &ctx);
    assert(ctx.r5 == 8);
    for (const auto& entry : wdc::cheats::catalog) wdc::cheats::set_enabled(entry.cheat, false);
}
