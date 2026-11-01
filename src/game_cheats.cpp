#include "game_cheats.h"
#include "recomp.h"
#include <atomic>
#include <cstdint>

namespace wdc::cheats {
namespace {
std::array<std::atomic_bool, static_cast<std::size_t>(Cheat::Count)> flags{};
}
bool enabled(Cheat cheat) {
    const auto index = static_cast<std::size_t>(cheat);
    return index < flags.size() && flags[index].load(std::memory_order_relaxed);
}
void set_enabled(Cheat cheat, bool value) {
    const auto index = static_cast<std::size_t>(cheat);
    if (index < flags.size()) flags[index].store(value, std::memory_order_relaxed);
}
bool consume_ending() {
    return flags[static_cast<std::size_t>(Cheat::Ending)].exchange(false, std::memory_order_relaxed);
}
}

namespace {
// One aligned KSEG0 word in the 8 MiB guest RAM. A bad pointer leaves the
// accessor's loaded value alone instead of aborting the menu.
bool guest_word(uint8_t* rdram, uint32_t address, int32_t* value, uint32_t offset = 0) {
    if (!rdram || address < 0x80000000u || address > 0x807FFFFCu - offset || (address & 3u)) return false;
    const gpr base = int32_t(address);
    *value = MEM_W(offset, base);
    return true;
}

// USA menu accessors. Each hook runs on the game thread after the original
// lbu has placed the lock byte in v0, and before jr ra. ra still names the
// caller. The generator restores the MIPS JAL link at every direct call to
// these accessors, including save/progression calls; N64Recomp omits that
// write by default. A zero v0 means locked.
//
// The same accessors also feed the Controller Pak snapshot. Those callers
// are absent from the lists below, so a cheat cannot be copied into the note:
//   tracks  0x800648D0 and 0x8006491C pack byte 5 into the save image
//   cars    0x80064B44 and 0x80064B88 pack byte 0; 0x80064840 and 0x80064888
//           pack byte 1
//   teams   func_80056B18 writes byte 16 when byte 1 reads as unlocked
//   earned  0x80056A88 stores a new car only when byte 0 still reads locked
// Any other caller keeps the loaded byte. An unknown menu stays locked
// rather than leaking into the save. An allowlisted caller may store v0
// into its own menu object. Those stores are not the measured Pak packers.
// docs/cheats.md records which of those copies were not fully traced.
//
// Car record (20 bytes, model id word at +8 equals the slot). GT2 is models
// 0..16 and GT1 is models 17..33. Model 34 is the interceptor.
// Team object (40 bytes, id word at +0). GT2 ids are 0..7 and GT1 ids are
// 7..14. Id 7 is in both ranges. Track records use one flag for every record.
// Big-endian words use MEM_W. The settings UI publishes the flags from another
// thread; only the atomics cross that boundary. Replace these lists with
// named menu checks once the menus are decompiled.
template <std::size_t Count>
bool returns_to(const recomp_context* ctx, const uint32_t (&returns)[Count]) {
    const auto ra = uint32_t(ctx->r31);
    for (uint32_t candidate : returns)
        if (candidate == ra) return true;
    return false;
}
constexpr uint32_t car_menu[] = {
    0x80058148, 0x8005A728, 0x8005AED4, 0x8006B9D4,
};
constexpr uint32_t car_seen_menu[] = {
    0x80058030, 0x800580BC, 0x80059E60, 0x80059FB8, 0x8005A034, 0x8005A0C8, 0x8005A128,
};
constexpr uint32_t team_menu[] = {0x800596CC, 0x80059728, 0x80066584};
constexpr uint32_t track_menu[] = {
    0x80059374, 0x80059404, 0x8005EFBC, 0x8005EFFC,
    0x8005F078, 0x8005F0D4, 0x8005F1DC, 0x8005F23C,
};
constexpr uint32_t event_menu[] = {0x8005B298, 0x80066520};
constexpr uint32_t team_class_menu[] = {0x80058464, 0x8005AE80, 0x8005E788};
constexpr uint32_t restart_menu[] = {0x80045AD4, 0x80045AE8, 0x80046788};
constexpr wdc::cheats::Cheat venue_cheats[] = {
    wdc::cheats::Cheat::Hawaii, wdc::cheats::Cheat::LesGets,
    wdc::cheats::Cheat::LasVegas, wdc::cheats::Cheat::NewZealand,
    wdc::cheats::Cheat::Lisbon, wdc::cheats::Cheat::Rome,
    wdc::cheats::Cheat::Sydney, wdc::cheats::Cheat::Zurich,
    wdc::cheats::Cheat::Kyoto, wdc::cheats::Cheat::BlackForest,
};
bool car_cheat_applies(uint8_t* rdram, recomp_context* ctx) {
    int32_t model = 0;
    if (!guest_word(rdram, uint32_t(ctx->r4), &model, 8)) return false;
    const bool gt2 = model >= 0 && model <= 16 && wdc::cheats::enabled(wdc::cheats::Cheat::Gt2Cars);
    const bool gt1 = model >= 17 && model <= 33 && wdc::cheats::enabled(wdc::cheats::Cheat::Gt1Cars);
    return gt1 || gt2 || (model == 16 && wdc::cheats::enabled(wdc::cheats::Cheat::Challenger)) ||
        (model == 34 && wdc::cheats::enabled(wdc::cheats::Cheat::Interceptor));
}
}

extern "C" void wdc_cheat_car_available(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx || ctx->r2 != 0 || !returns_to(ctx, car_menu)) return;
    if (car_cheat_applies(rdram, ctx)) ctx->r2 = 1;
}
// Byte 1 is the companion flag written with byte 0 by func_800565BC. The ROM
// has no name for it. Menu code reads it through func_800565EC.
extern "C" void wdc_cheat_car_seen(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx || ctx->r2 != 0 || !returns_to(ctx, car_seen_menu)) return;
    if (car_cheat_applies(rdram, ctx)) ctx->r2 = 1;
}
extern "C" void wdc_cheat_team_available(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx || ctx->r2 != 0 || !returns_to(ctx, team_menu)) return;
    int32_t id = 0;
    if (!guest_word(rdram, uint32_t(ctx->r4), &id)) return;
    const bool gt2 = id >= 0 && id <= 7 && wdc::cheats::enabled(wdc::cheats::Cheat::Gt2Cars);
    const bool gt1 = id >= 7 && id <= 14 && wdc::cheats::enabled(wdc::cheats::Cheat::Gt1Cars);
    if (gt1 || gt2 || (id == 6 && wdc::cheats::enabled(wdc::cheats::Cheat::Challenger)) ||
        (id == 12 && wdc::cheats::enabled(wdc::cheats::Cheat::Interceptor))) ctx->r2 = 1;
}
extern "C" void wdc_cheat_track_available(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx || !rdram || ctx->r2 != 0 || !returns_to(ctx, track_menu)) return;
    if (wdc::cheats::enabled(wdc::cheats::Cheat::Tracks)) {
        ctx->r2 = 1;
        return;
    }
    // Track record: big-endian u32 layout id at +0, followed by flag bytes.
    // The ten name-password ranges cover ids 0..57; the last has four layouts.
    int32_t id = -1;
    if (guest_word(rdram, uint32_t(ctx->r4), &id) && id >= 0 && id < 58 &&
        wdc::cheats::enabled(venue_cheats[id / 6])) ctx->r2 = 1;
}

// Event record: 112 bytes, u32 event id at +0x50, unlocked byte at +0x4C.
// Only the two menu checks above can see a session unlock. The loaded byte
// and id belong to the career; neither the record nor the Pak image is changed.
extern "C" void wdc_cheat_event_available(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx || ctx->r2 != 0 || !returns_to(ctx, event_menu)) return;
    int32_t id = -1;
    if (!guest_word(rdram, uint32_t(ctx->r4), &id, 0x50)) return;
    if ((id >= 0 && id < 10 && wdc::cheats::enabled(wdc::cheats::Cheat::Gt2Events)) ||
        (id >= 10 && id < 20 && wdc::cheats::enabled(wdc::cheats::Cheat::Gt1Events))) ctx->r2 = 1;
}

// Boss team id 12 uses the word at +20 to offer the Interceptor (class 3).
// Its race-setup reader at 800681F4 is deliberately excluded: that path can
// advance the original career. These three callers build/select menu cars.
extern "C" void wdc_cheat_team_class(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx || !returns_to(ctx, team_class_menu) ||
        !wdc::cheats::enabled(wdc::cheats::Cheat::Interceptor)) return;
    int32_t id = -1;
    if (guest_word(rdram, uint32_t(ctx->r4), &id) && id == 12) ctx->r2 = 3;
}

extern "C" void wdc_cheat_restart_count(uint8_t*, recomp_context* ctx) {
    if (ctx && returns_to(ctx, restart_menu) &&
        wdc::cheats::enabled(wdc::cheats::Cheat::InfiniteRestarts)) ctx->r2 = 9;
}

// func_80066648 stores the next credit balance. When a restart is charged,
// preserve the real big-endian u32 at career+0x1060 instead of storing the
// menu's synthetic 9-1. The ROM still handles an original infinite flag.
// Read on the game thread before the setter; invalid memory leaves a1 alone.
extern "C" void wdc_cheat_restart_spend(uint8_t* rdram, recomp_context* ctx) {
    if (!ctx || uint32_t(ctx->r31) != 0x80045AFC ||
        !wdc::cheats::enabled(wdc::cheats::Cheat::InfiniteRestarts)) return;
    int32_t balance = 0;
    if (guest_word(rdram, uint32_t(ctx->r4), &balance, 0x1060)) ctx->r5 = balance;
}

// Register-only hooks after the documented ROM loads/comparisons. Each
// original value is a guest u32 flag (or signed strcmp result for EVILROB).
// They run on the consuming game thread, retain no pointers, and never
// change native flags or saved passwords. Disabled means original behavior.
#define CHEAT_READ(name, cheat, value) \
    extern "C" uint64_t name(uint64_t original) { \
        return wdc::cheats::enabled(wdc::cheats::Cheat::cheat) ? uint64_t(value) : original; \
    }
CHEAT_READ(wdc_cheat_pink, PinkCars, 1)
CHEAT_READ(wdc_cheat_black, BlackCars, 1)
CHEAT_READ(wdc_cheat_small, SmallCars, 1)
CHEAT_READ(wdc_cheat_visible, InvisibleCars, 0)
CHEAT_READ(wdc_cheat_red_skids, RedSkidmarks, 1)
CHEAT_READ(wdc_cheat_retro, RetroGraphics, 0)
CHEAT_READ(wdc_cheat_clear_scene, ClearScene, 1)
CHEAT_READ(wdc_cheat_mirror, MirrorChampionship, 1)
CHEAT_READ(wdc_cheat_race_select, RaceSelect, 1)
CHEAT_READ(wdc_cheat_evilrob, EvilRob, 0)
#undef CHEAT_READ

// Preserve native fade alpha while a transition is active. Only the two
// framebuffer consumers see the overlay's blur; race fade timers stay native.
extern "C" uint64_t wdc_cheat_blur(uint64_t original) {
    return original == 0 && wdc::cheats::enabled(wdc::cheats::Cheat::SuperBlur) ? 0xA0 : original;
}

// One-shot request, consumed only by the event menu's original ending check.
// No write to career+0x10FD; a native ending request remains effective too.
extern "C" void wdc_cheat_ending(uint8_t*, recomp_context* ctx) {
    if (ctx && uint32_t(ctx->r31) == 0x800592D4 &&
        wdc::cheats::consume_ending()) ctx->r2 = 1;
}
