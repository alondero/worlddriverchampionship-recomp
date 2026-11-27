#include "game_scenario.h"
#include "game_visibility.h"
#include "race_globals.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <optional>
#include <string>
#include <vector>

namespace wdc::scenario {
namespace {
namespace race = wdc::render::race;
constexpr unsigned max_budget = 36000;
enum class Start { Boot, Menu, Race };
struct Run {
    unsigned end;
    uint16_t buttons;
    float x, y;
};
struct Position { double x, y, z; };
struct Race {
    uint32_t course, layout, car, mode, laps;
    uint32_t last_sample = 0;
    unsigned samples = 0;
    double max_displacement = 0;
    unsigned peak_visibility_list_count = 0;
    unsigned peak_visibility_filled_slots = 0;
    uint32_t min_frame_arena_gap = std::numeric_limits<uint32_t>::max();
    uint32_t min_secondary_frame_arena_gap = std::numeric_limits<uint32_t>::max();
    std::optional<Position> origin;
};

// Initialized before runtime threads start and immutable thereafter.
std::vector<Run> runs;
unsigned max_vis;
Start start;
bool capture_window;
std::atomic<bool> started{};
std::atomic<unsigned> polls{}, vis{}, tasks{}, presentations{};

// The controller worker owns its cursor. The dispatcher owns observations.
std::size_t input_run;
unsigned menu_ticks, tail_ticks;
uint32_t last_callback;
bool in_race, waiting_for_capture;
std::vector<Race> races;

bool bad_script() {
    std::fprintf(stderr, "[wdc:scenario] invalid scenario input file\n");
    return false;
}

void finish(unsigned consumed) {
    std::ofstream result("scenario-native.json");
    result.imbue(std::locale::classic());
    result << "{\"schema\":1,\"input_polls\":" << consumed
           << ",\"vis\":" << vis.load() << ",\"menu_ticks\":" << menu_ticks
           << ",\"graphics_tasks\":" << tasks.load() << ",\"presentations\":" << presentations.load()
           << ",\"races\":[";
    const char* separator = "";
    for (const auto& entry : races) {
        result << separator << "{\"course\":" << entry.course << ",\"layout\":" << entry.layout
               << ",\"car\":" << entry.car << ",\"mode\":" << entry.mode << ",\"laps\":" << entry.laps
               << ",\"samples\":" << entry.samples << ",\"displacement\":" << entry.max_displacement
               << ",\"peak_visibility_list_count\":" << entry.peak_visibility_list_count
               << ",\"peak_visibility_filled_slots\":" << entry.peak_visibility_filled_slots
               << ",\"frame_arena_gap\":" << entry.min_frame_arena_gap
               << ",\"secondary_frame_arena_gap\":" << entry.min_secondary_frame_arena_gap << '}';
        separator = ",";
    }
    result << "]}\n";
    result.close();
    if (!result) {
        std::fprintf(stderr, "[wdc:scenario] could not write scenario-native.json\n");
        std::_Exit(4);
    }
    std::fflush(nullptr);
    if (!capture_window) std::_Exit(0);
    waiting_for_capture = true;
}

// Read-only USA RDRAM observations on the dispatcher before its indirect
// call at 80001D98. race_globals.h documents the word-swapped u32 globals.
// 80092360 is the native race setup. Its u32 first human model (+08),
// course/layout (+70/+74), lap count (+80) and mode (+84) come from 80068AA8.
// With one human, roster slot zero owns the body matrix at 800BA804. Its
// aligned f32 translation (+30/+34/+38) is measured in world units. Sample
// only after the race loader, on finite positions, with a live roster.
// Ordinary race reads stay inside 8 MiB. The visibility entries use the
// host-reserved extended list documented below; no guest pointers survive the tick.
void observe_race(const uint8_t* rdram) {
    constexpr uint32_t config = 0x80092360, body = 0x800BA804;
    const auto sample = race::word(rdram, race::sample);
    if (!in_race) {
        auto& entry = races.emplace_back(
            race::word(rdram, config + 0x70), race::word(rdram, config + 0x74),
            race::word(rdram, config + 8), race::word(rdram, race::scene),
            race::word(rdram, config + 0x80));
        entry.last_sample = sample;
        std::fprintf(stderr, "[wdc:scenario] race course=%u layout=%u car=%u mode=%u laps=%u\n",
                     entry.course, entry.layout, entry.car, entry.mode, entry.laps);
    }
    auto& entry = races.back();
    if (sample >= entry.last_sample) entry.samples += sample - entry.last_sample;
    entry.last_sample = sample;

    // The visibility list count is game-owned at 8007E920. Its entries were
    // relocated to the port's Expansion Pak region; at this dispatcher
    // boundary they describe the most recently culled frame. Read both peaks
    // without retaining guest pointers. Out-of-range counts are loader noise.
    constexpr uint32_t visibility_count = 0x8007E920;
    constexpr uint32_t visibility_list = wdc::visibility::track_section_list;
    constexpr uint32_t visibility_capacity = wdc::visibility::max_track_sections;
    const auto count = race::word(rdram, visibility_count);
    if (count <= visibility_capacity) {
        entry.peak_visibility_list_count = std::max<unsigned>(entry.peak_visibility_list_count, count);
        unsigned filled = 0;
        for (uint32_t index = 0; index < count; ++index)
            filled += race::word(rdram, visibility_list + index * 4) != 0;
        entry.peak_visibility_filled_slots = std::max(entry.peak_visibility_filled_slots, filled);
    }

    // 8007ED40 is the ascending command cursor and 8007FE70 the descending
    // matrix/packet cursor for the ROM's race frame arena. They move toward
    // each other within one game-owned allocation; positive space between them
    // is headroom, while zero here means a collision happened or was observed
    // after the cursors crossed. Sampled at the dispatcher boundary only.
    const uint32_t ascending = race::word(rdram, 0x8007ED40);
    const uint32_t descending = race::word(rdram, 0x8007FE70);
    const uint32_t gap = descending >= ascending ? descending - ascending : 0;
    entry.min_frame_arena_gap = std::min(entry.min_frame_arena_gap, gap);

    // 8007ED84/8007FE74 are the same cursor directions for the secondary
    // packet arena used by HUD and other small raw-list packets.
    const uint32_t secondary_ascending = race::word(rdram, 0x8007ED84);
    const uint32_t secondary_descending = race::word(rdram, 0x8007FE74);
    const uint32_t secondary_gap = secondary_descending >= secondary_ascending
        ? secondary_descending - secondary_ascending : 0;
    entry.min_secondary_frame_arena_gap = std::min(entry.min_secondary_frame_arena_gap, secondary_gap);
    if (race::word(rdram, config) != 1) return;
    const double x = std::bit_cast<float>(race::word(rdram, body + 0x30));
    const double y = std::bit_cast<float>(race::word(rdram, body + 0x34));
    const double z = std::bit_cast<float>(race::word(rdram, body + 0x38));
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if (!entry.origin) entry.origin = Position{x, y, z};
    const double displacement = std::hypot(x - entry.origin->x, y - entry.origin->y, z - entry.origin->z);
    entry.max_displacement = std::max(entry.max_displacement, displacement);
}
}

bool configure() {
    const char* path = std::getenv("WDC_SCENARIO_INPUT");
    if (!path) return true;
    capture_window = std::getenv("WDC_SCENARIO_CAPTURE") != nullptr;
    std::ifstream script(path);
    script.imbue(std::locale::classic());
    std::string magic, gate;
    int budget;
    if (!(script >> magic >> gate >> budget) || magic != "WDC_SCENARIO_1" ||
        budget <= 0 || unsigned(budget) > max_budget) return bad_script();
    max_vis = unsigned(budget);
    if (gate == "boot") start = Start::Boot;
    else if (gate == "menu") start = Start::Menu;
    else if (gate == "race") start = Start::Race;
    else return bad_script();

    unsigned end = 0;
    while (script >> std::ws && script.peek() != EOF) {
        int count, buttons;
        float x, y;
        if (!(script >> count >> buttons >> x >> y) || count <= 0 || unsigned(count) > max_budget - end ||
            buttons < 0 || buttons > UINT16_MAX || !std::isfinite(x) || !std::isfinite(y) ||
            std::abs(x) > 1 || std::abs(y) > 1) return bad_script();
        end += unsigned(count);
        runs.push_back({end, static_cast<uint16_t>(buttons), x, y});
    }
    if (script.bad() || runs.empty()) return bad_script();
    started.store(start == Start::Boot);
    return true;
}

bool enabled() { return !runs.empty(); }

// N64ModernRuntime calls get_input on the controller worker while filling
// the pad array. A run lasts a count of actual port-one reads, independent
// of rendering, keyboard focus and the wall clock. The SDK still converts
// normalized stick values and writes the guest pads.
bool read_input(int port, uint16_t* buttons, float* x, float* y) {
    *buttons = 0;
    *x = *y = 0;
    if (port != 0) return false;
    if (!started.load(std::memory_order_acquire) || input_run == runs.size()) return true;
    const auto& run = runs[input_run];
    *buttons = run.buttons;
    *x = run.x;
    *y = run.y;
    const auto consumed = polls.load(std::memory_order_relaxed) + 1;
    if (consumed == run.end) ++input_run;
    polls.store(consumed, std::memory_order_release);
    return true;
}

void dispatcher_tick(const uint8_t* rdram) {
    if (!enabled() || !rdram) return;
    if (waiting_for_capture) {
        std::error_code error;
        if (std::filesystem::exists("capture.done", error)) std::_Exit(0);
        return;
    }
    const auto callback = race::word(rdram, race::callback);
    if (callback != last_callback) {
        std::fprintf(stderr, "[wdc:scenario] dispatcher callback=%08x\n", callback);
        last_callback = callback;
    }
    const auto mode = race::word(rdram, race::scene);
    const bool menu = callback == 0x8004E950;
    const bool racing = race::active(rdram) && mode < 0x11 && race::word(rdram, race::cars);
    if (menu) ++menu_ticks;
    if (racing) observe_race(rdram);
    in_race = racing;
    if ((start == Start::Menu && menu) || (start == Start::Race && racing)) {
        if (!started.exchange(true, std::memory_order_release))
            std::fprintf(stderr, "[wdc:scenario] controller replay started\n");
    }
    const auto consumed = polls.load(std::memory_order_acquire);
    // Let the final poll pass through the input worker and at least one
    // complete dispatcher iteration before publishing the observations.
    if (consumed == runs.back().end && ++tail_ticks >= 3) finish(consumed);
}

void vi_tick(unsigned vi) {
    if (!enabled()) return;
    vis.store(vi, std::memory_order_relaxed);
    if (vi < max_vis) return;
    std::fprintf(stderr, "[wdc:scenario] VI budget exhausted at %u (%u/%u input polls)\n",
                 vi, polls.load(), runs.back().end);
    std::fflush(nullptr);
    std::_Exit(3);
}

void graphics_task() {
    if (enabled()) tasks.fetch_add(1, std::memory_order_relaxed);
}
void presentation() {
    if (enabled()) presentations.fetch_add(1, std::memory_order_relaxed);
}
}
