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
}
