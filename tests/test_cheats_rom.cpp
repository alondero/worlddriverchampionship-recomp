#undef NDEBUG
#include "game_cheats.h"
#include "recomp.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

extern "C" {
void func_800565E0(uint8_t*, recomp_context*);
void func_800565EC(uint8_t*, recomp_context*);
void func_80056974(uint8_t*, recomp_context*);
void func_80056B90(uint8_t*, recomp_context*);
void func_80057000(uint8_t*, recomp_context*);
void func_80057420(uint8_t*, recomp_context*);
void func_8006663C(uint8_t*, recomp_context*);
void func_80066648(uint8_t*, recomp_context*);
void func_8006969C(uint8_t*, recomp_context*);
void func_800696A8(uint8_t*, recomp_context*);
void func_80066558(uint8_t*, recomp_context*);
void func_800664F4(uint8_t*, recomp_context*);
void func_80056B18(uint8_t*, recomp_context*);
}

namespace {
using wdc::cheats::Cheat;
using Function = void (*)(uint8_t*, recomp_context*);
constexpr int32_t career = int32_t(0x80097D00);
constexpr size_t career_offset = uint32_t(career) & 0x1FFFFFFF;
constexpr size_t career_size = 0x1104; // Through the last career flag at +0x1100.
constexpr int32_t car = career + 0xB4C, team = career + 0x2C;
constexpr int32_t track = career + 0xE08, event = career + 0x288;
constexpr uint32_t car_menu[] = {0x80058148, 0x8005A728, 0x8005AED4, 0x8006B9D4};
constexpr uint32_t seen_menu[] = {
    0x80058030, 0x800580BC, 0x80059E60, 0x80059FB8,
    0x8005A034, 0x8005A0C8, 0x8005A128,
};
constexpr uint32_t team_menu[] = {0x800596CC, 0x80059728, 0x80066584};
constexpr uint32_t class_menu[] = {0x80058464, 0x8005AE80, 0x8005E788};
constexpr uint32_t track_menu[] = {
    0x80059374, 0x80059404, 0x8005EFBC, 0x8005EFFC,
    0x8005F078, 0x8005F0D4, 0x8005F1DC, 0x8005F23C,
};
constexpr uint32_t event_menu[] = {0x8005B298, 0x80066520};
constexpr uint32_t restart_menu[] = {0x80045AD4, 0x80045AE8, 0x80046788};
constexpr Cheat venues[] = {
    Cheat::Hawaii, Cheat::LesGets, Cheat::LasVegas, Cheat::NewZealand,
    Cheat::Lisbon, Cheat::Rome, Cheat::Sydney, Cheat::Zurich,
    Cheat::Kyoto, Cheat::BlackForest,
};

void reset_options() {
    for (const auto& entry : wdc::cheats::catalog)
        wdc::cheats::set_enabled(entry.cheat, false);
}
}

int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024, 0xA5);
    auto* rdram = memory.data();
    auto copy_career = [&] {
        std::array<uint8_t, career_size> before;
        std::copy_n(memory.begin() + career_offset, career_size, before.begin());
        return before;
    };
    auto assert_career = [&](const auto& before) {
        assert(std::equal(before.begin(), before.end(), memory.begin() + career_offset));
    };
    // Each call runs the actual generated accessor and hook, including the
    // guest load that supplies v0. Sentinels make missing/wrong loads visible.
    auto call = [&](Function function, int32_t object, uint32_t ra, int argument = 0) {
        recomp_context ctx{};
        ctx.r2 = 0x12345678;
        ctx.r4 = object;
        ctx.r5 = argument;
        ctx.r29 = int32_t(0x807FF000);
        ctx.r31 = int32_t(ra);
        function(rdram, &ctx);
        return int64_t(ctx.r2);
    };
    auto read = [&](Function function, int32_t object, uint32_t ra) {
        const auto before = copy_career();
        const auto result = call(function, object, ra);
        assert_career(before); // All career records and flags, including neighbours.
        return result;
    };

    reset_options();
    MEM_B(0, car) = 0; MEM_B(1, car) = 0;
    MEM_B(16, team) = 0; MEM_W(20, team) = 0;
    MEM_B(5, track) = 0; MEM_B(0x4C, event) = 0;
    MEM_W(0x1060, career) = 4; MEM_B(0x10FF, career) = 0;
    MEM_B(0x10FE, career) = 0; MEM_B(0x10FD, career) = 0;

    // Both special car passwords use the same original read paths as the
    // ordinary class unlocks. Their menu allowlists must not affect Pak reads.
    for (const auto [option, model, team_id] : {
            std::array<int, 3>{int(Cheat::Challenger), 16, 6},
            std::array<int, 3>{int(Cheat::Interceptor), 34, 12}}) {
        MEM_W(8, car) = model; MEM_W(0, team) = team_id;
        for (const bool enabled : {false, true, false}) {
            wdc::cheats::set_enabled(Cheat(option), enabled);
            for (auto ra : car_menu) assert(read(func_800565E0, car, ra) == int(enabled));
            for (auto ra : seen_menu) assert(read(func_800565EC, car, ra) == int(enabled));
            for (auto ra : team_menu) assert(read(func_80056974, team, ra) == int(enabled));
            for (auto ra : {0x80064B4Cu, 0x80064B90u, 0x80056A90u, 0u})
                assert(read(func_800565E0, car, ra) == 0);
            for (auto ra : {0x80064848u, 0x80064890u, 0x80056B50u, 0u})
                assert(read(func_800565EC, car, ra) == 0);
            assert(read(func_80056974, team, 0) == 0);
        }
    }

    for (auto option : {Cheat::Gt2Cars, Cheat::Gt1Cars}) {
        wdc::cheats::set_enabled(option, true);
        for (int model = -1; model <= 35; ++model) {
            MEM_W(8, car) = model;
            const bool group = option == Cheat::Gt2Cars ? model >= 0 && model <= 16 :
                                                        model >= 17 && model <= 33;
            assert(read(func_800565E0, car, car_menu[0]) == int(group));
            assert(read(func_800565EC, car, seen_menu[0]) == int(group));
        }
        wdc::cheats::set_enabled(option, false);
    }
    wdc::cheats::set_enabled(Cheat::Interceptor, true);
    MEM_W(8, car) = 34; MEM_W(0, team) = 12;
    MEM_B(0, car) = 2; MEM_B(1, car) = 2; MEM_B(16, team) = 2;
    assert(read(func_800565E0, car, car_menu[0]) == 2);
    assert(read(func_800565EC, car, seen_menu[0]) == 2);
    assert(read(func_80056974, team, team_menu[0]) == 2);
    MEM_B(0, car) = 0; MEM_B(1, car) = 0; MEM_B(16, team) = 0;
    reset_options();

    // Only Boss team 12 gets the Interceptor's fourth class; the race setup
    // reader remains unchanged to prevent synthetic menu state advancing save.
    for (int id : {11, 12, 13}) {
        MEM_W(0, team) = id;
        for (const bool enabled : {false, true, false}) {
            wdc::cheats::set_enabled(Cheat::Interceptor, enabled);
            for (auto ra : class_menu)
                assert(read(func_80056B90, team, ra) == (enabled && id == 12 ? 3 : 0));
            assert(read(func_80056B90, team, 0x800681FC) == 0);
            assert(read(func_80056B90, team, 0) == 0);
        }
    }

    // Two independent event groups, using the event id at +0x50 rather than
    // the track or an adjacent career field. Existing availability survives.
    for (auto option : {Cheat::Gt2Events, Cheat::Gt1Events}) {
        for (const bool enabled : {false, true, false}) {
            wdc::cheats::set_enabled(option, enabled);
            for (int id = -1; id <= 20; ++id) {
                MEM_W(0x50, event) = id;
                const bool group = option == Cheat::Gt2Events ? id >= 0 && id < 10 : id >= 10 && id < 20;
                for (auto ra : event_menu)
                    assert(read(func_80057420, event, ra) == int(enabled && group));
                assert(read(func_80057420, event, 0) == 0);
            }
        }
    }
    wdc::cheats::set_enabled(Cheat::Gt1Events, true);
    MEM_B(0x4C, event) = 2; MEM_W(0x50, event) = 10;
    assert(read(func_80057420, event, event_menu[0]) == 2);
    reset_options();

    // The ten original name ranges partition layouts 0..57. Every other
    // venue and out-of-range id remains locked; each option is reversible.
    for (int venue = 0; venue < 10; ++venue) {
        wdc::cheats::set_enabled(venues[venue], true);
        for (int id = -1; id <= 58; ++id) {
            MEM_W(0, track) = id;
            const int expected = id >= 0 && id < 58 && id / 6 == venue;
            assert(read(func_80057000, track, track_menu[0]) == expected);
            assert(read(func_80057000, track, 0x800648D8) == 0);
            assert(read(func_80057000, track, 0x80064924) == 0);
        }
        MEM_W(0, track) = venue * 6;
        for (auto ra : track_menu) assert(read(func_80057000, track, ra) == 1);
        wdc::cheats::set_enabled(venues[venue], false);
        assert(read(func_80057000, track, track_menu[0]) == 0);
    }
    wdc::cheats::set_enabled(Cheat::Tracks, true);
    MEM_W(0, track) = 99;
    for (auto ra : track_menu) assert(read(func_80057000, track, ra) == 1);
    assert(read(func_80057000, track, 0) == 0);
    MEM_B(5, track) = 2;
    assert(read(func_80057000, track, track_menu[0]) == 2);
    reset_options();

    // The UI sees nine, while Controller Pak packing sees the earned balance.
    // A charged restart preserves that balance using the actual ROM setter.
    for (int balance = 0; balance <= 9; ++balance) {
        MEM_W(0x1060, career) = balance;
        for (const bool enabled : {false, true, false}) {
            wdc::cheats::set_enabled(Cheat::InfiniteRestarts, enabled);
            for (auto ra : restart_menu)
                assert(read(func_8006663C, career, ra) == (enabled ? 9 : balance));
            assert(read(func_8006663C, career, 0x80064B24) == balance);
            assert(read(func_8006663C, career, 0) == balance);
        }
        wdc::cheats::set_enabled(Cheat::InfiniteRestarts, true);
        const auto before = copy_career();
        call(func_80066648, career, 0x80045AFC, 8);
        assert_career(before);
    }
    // Other callers still set earned credits normally, even with the toggle.
    call(func_80066648, career, 0, 3);
    assert(MEM_W(0x1060, career) == 3);
    wdc::cheats::set_enabled(Cheat::InfiniteRestarts, false);
    call(func_80066648, career, 0x80045AFC, 2);
    assert(MEM_W(0x1060, career) == 2);
    // The original infinite flag keeps its original setter behavior.
    MEM_B(0x10FF, career) = 1;
    call(func_80066648, career, 0, 1);
    assert(MEM_W(0x1060, career) == 9);

    for (const bool enabled : {false, true, false}) {
        wdc::cheats::set_enabled(Cheat::RaceSelect, enabled);
        assert(read(func_8006969C, career, 0) == int(enabled));
    }
    MEM_B(0x10FE, career) = 2;
    assert(read(func_8006969C, career, 0) == 2);

    // An unrelated read must not consume the one-shot ending request.
    assert(read(func_800696A8, career, 0x800592D4) == 0);
    wdc::cheats::set_enabled(Cheat::Ending, true);
    wdc::cheats::set_enabled(Cheat::Ending, false);
    assert(read(func_800696A8, career, 0x800592D4) == 0);
    wdc::cheats::set_enabled(Cheat::Ending, true);
    assert(read(func_800696A8, career, 0) == 0);
    assert(wdc::cheats::enabled(Cheat::Ending));
    assert(read(func_800696A8, career, 0x800592D4) == 1);
    assert(!wdc::cheats::enabled(Cheat::Ending));
    assert(read(func_800696A8, career, 0x800592D4) == 0);
    MEM_B(0x10FD, career) = 1;
    assert(read(func_800696A8, career, 0x800592D4) == 1);
    assert(read(func_800696A8, career, 0x800592D4) == 1);
    reset_options();

    // Check all of RDRAM once for a representative enabled accessor scenario.
    // Fine-grained permutations above compare the full career, avoiding an
    // 8 MiB allocation/copy/comparison for every model, venue and caller.
    MEM_W(8, car) = 34; MEM_B(0, car) = 0; MEM_B(1, car) = 0;
    MEM_W(0, team) = 12; MEM_B(16, team) = 0; MEM_W(20, team) = 0;
    MEM_W(0, track) = 0; MEM_B(5, track) = 0;
    MEM_W(0x50, event) = 10; MEM_B(0x4C, event) = 0;
    MEM_W(0x1060, career) = 4;
    MEM_B(0x10FD, career) = 0; MEM_B(0x10FE, career) = 0;
    for (const auto& entry : wdc::cheats::catalog)
        wdc::cheats::set_enabled(entry.cheat, true);
    struct Read { Function function; int32_t record; uint32_t ra; int expected; };
    const Read representative[] = {
        {func_800565E0, car, car_menu[0], 1},
        {func_800565EC, car, seen_menu[0], 1},
        {func_80056974, team, team_menu[0], 1},
        {func_80056B90, team, class_menu[0], 3},
        {func_80057000, track, track_menu[0], 1},
        {func_80057420, event, event_menu[0], 1},
        {func_8006663C, career, restart_menu[0], 9},
        {func_8006969C, career, 0, 1},
        {func_800696A8, career, 0x800592D4, 1},
    };
    const auto full_before = memory;
    for (const auto& sample : representative)
        assert(call(sample.function, sample.record, sample.ra) == sample.expected);
    assert(memory == full_before);
    reset_options();

    // Enter real translated menu callers with an arbitrary stale ra. Their
    // JALs must publish the caller's actual continuation before each accessor
    // hook runs; manually seeding the accessor's ra cannot catch this bug.
    // These callers use stack scratch, so compare the complete career region.
    auto read_career = [&](Function function, int32_t object, uint32_t stale_ra) {
        const auto before = copy_career();
        const auto result = call(function, object, stale_ra);
        assert_career(before);
        return result;
    };
    for (int i = 0; i < 8; ++i) {
        const int32_t gt1_team = career + 0x144 + i * 40;
        MEM_W(0, gt1_team) = i + 7;
        MEM_B(16, gt1_team) = 0;
        const int32_t gt1_event = career + 0x6E8 + i * 112;
        MEM_W(0x50, gt1_event) = i + 10;
        MEM_B(0x4C, gt1_event) = 0;
    }
    for (uint32_t stale_ra : {0xDEADBEEFu, 0x80058030u}) {
        for (const bool enabled : {false, true, false}) {
            wdc::cheats::set_enabled(Cheat::Gt1Cars, enabled);
            wdc::cheats::set_enabled(Cheat::Gt1Events, enabled);
            assert(read_career(func_80066558, career, stale_ra) == int(enabled));
            assert(read_career(func_800664F4, career, stale_ra) == int(enabled));
        }
    }

    // The real team-progression caller also needs its own JAL continuation.
    // An inherited allowlisted ra would otherwise promote a synthetic car
    // unlock into the team's saved availability byte.
    const int32_t progressing_team = career + 0x144;
    MEM_W(0, progressing_team) = 7;
    MEM_W(4, progressing_team) = car;
    MEM_W(20, progressing_team) = 1;
    MEM_B(16, progressing_team) = 0;
    MEM_W(8, car) = 17;
    MEM_B(1, car) = 0;
    for (auto option : {Cheat::Gt1Cars, Cheat::Gt2Cars, Cheat::Challenger, Cheat::Interceptor})
        wdc::cheats::set_enabled(option, true);
    read_career(func_80056B18, progressing_team, 0x80058030);
    assert(MEM_BU(16, progressing_team) == 0);

    // Earned availability still advances the original team record. Compare
    // against exactly that one expected career-byte change.
    MEM_B(1, car) = 1;
    MEM_B(16, progressing_team) = 1;
    const auto earned = copy_career();
    MEM_B(16, progressing_team) = 0;
    call(func_80056B18, progressing_team, 0x80058030);
    assert_career(earned);
    reset_options();
}
