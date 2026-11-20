#include "display_rate_draw.h"
#include "extra_race_draw.h"
#include "guest_context.h"
#include "graphics_config.h"
#include "race_frame_schedule.h"
#include "race_poses.h"
#include "race_render_sample.h"
#include "race_swap.h"
#include "race_globals.h"
#include "render_statistics.h"
#include "render_memory.h"
#include "savestate.h"
#include "librecomp/queue_memory.hpp"
#include "ultramodern/swap_presentation.hpp"
#include "ultramodern/ultramodern.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

extern "C" void func_8000A30C(uint8_t*, recomp_context*);
extern "C" void func_800252D0(uint8_t*, recomp_context*);

namespace wdc::render {
namespace {
using Clock = std::chrono::steady_clock;
DisplayRate requested_rate() {
    static const DisplayRate rate = [] {
        const char* value = std::getenv("WDC_EXPERIMENTAL_DISPLAY_HZ");
        DisplayRate result{0, 1};
        if (!value) return result;
        std::string_view input(value);
        const auto end = input.data() + input.size();
        auto [next, error] = std::from_chars(value, end, result.numerator);
        if (error == std::errc{} && next != end && *next == '/') {
            auto denominator = std::from_chars(next + 1, end, result.denominator);
            next = denominator.ptr;
            error = denominator.ec;
        }
        if (error != std::errc{} || next != end || !result.supported()) {
            std::fprintf(stderr, "[wdc:render] invalid WDC_EXPERIMENTAL_DISPLAY_HZ\n");
            return DisplayRate{0, 1};
        }
        return result;
    }();
    if (rate.numerator) return rate;
    const auto settings = graphics::snapshot();
    if (!settings.interpolate_races) return {0, 1};
    if (settings.rate == -1) {
        const unsigned hz = ultramodern::get_display_refresh_rate();
        DisplayRate display{hz, 1};
        return display.supported() ? display : DisplayRate{0, 1};
    }
    const auto custom = DisplayRate::from_millihertz(unsigned(std::lround(settings.rate * 1000)));
    return custom.supported() ? custom : DisplayRate{0, 1};
}

// Renderer worker TLS is bound to each explicit screen action by the runtime.
thread_local bool explicit_update = false;
// One owner per dispatcher thread. Renderer-worker TLS above is independent.
struct RaceDrawer {
    bool presenting = false, failed = false;
    std::optional<RacePoses> previous;
    std::optional<uint32_t> last_sample;
    uint8_t* live_image = nullptr;
    Clock::time_point deadline{};
    std::optional<Clock::time_point> draw_started;
    int64_t draw_ns = 0;
    DisplayRate active_rate{};
    RaceFrameSchedule schedule;
    race::DispatcherHistory history;

    void set_presenting(bool enabled) {
        if (presenting == enabled) return;
        ultramodern::set_swap_presentation(enabled, {[](bool active) { explicit_update = active; }});
        presenting = enabled;
    }
    void end_race_presentation() {
        set_presenting(false);
        previous.reset();
        last_sample.reset();
        draw_started.reset();
        draw_ns = 0;
    }
    bool draw(uint8_t* rdram, recomp_context& caller);
};
thread_local RaceDrawer drawer;

void pace() {
    auto now = Clock::now();
    if (drawer.draw_started) {
        drawer.draw_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - *drawer.draw_started).count();
        drawer.draw_started.reset();
    }
    while (now < drawer.deadline) {
        ultramodern::wait_for_external_message_timed(drawer.live_image, 1);
        now = Clock::now();
    }
}

struct FenceUnavailable {};
void fence(uint8_t* live) {
    if (!ultramodern::wait_for_submitted_graphics(live)) throw FenceUnavailable{};
}

}

// USA 80001D90, same dispatcher thread, after any checkpoint restore and
// before the indirect game/menu/loader callback. Keep retrace presentation
// disabled between active draws, including fractional groups without a native
// endpoint. Release it before handing the display back to another scene.
extern "C" void wdc_render_dispatcher_tick(uint8_t* rdram) {
    const bool restored = drawer.history.restored_while_presenting(drawer.presenting, state::restore_generation());
    if (drawer.presenting && (restored || !rdram || !race::active(rdram)))
        drawer.end_race_presentation();
}

bool RaceDrawer::draw(uint8_t* rdram, recomp_context& caller) {
    const auto rate = requested_rate();
    if (!rate.numerator || failed) {
        if (presenting) end_race_presentation();
        return false;
    }
    if (active_rate.numerator != rate.numerator || active_rate.denominator != rate.denominator) {
        end_race_presentation();
        active_rate = rate;
    }
    if (!rdram || !race::eligible(rdram)) {
        end_race_presentation();
        return false;
    }

    const uint32_t sample = race::word(rdram, race::sample);
    if (last_sample == sample) {
        end_race_presentation();
        last_sample = sample;
        return false;
    }
    const auto start = Clock::now();
    deadline = start;
    live_image = rdram;
    set_presenting(true);
    bool drawn = false;
    try {
        {
            RaceSwap pending(rdram, previous ? nullptr : pace);
            capture_race_sample(rdram, caller, false, fence);
        }
        auto& bank = race_render_bank();
        auto current = RacePoses::capture(bank, true, true);
        if (!current) {
            end_race_presentation();
            return false;
        }
        const auto continuity = current->compare_with(previous ? *previous : *current);
        const bool reset = !previous || continuity.reset();
        if (!schedule.begin_group(rate, continuity.sample_gap, reset, true)) {
            end_race_presentation();
            return false;
        }
        const auto& grid = schedule.group();
        const auto capture_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        if (reset) draw_ns = 0;
        const auto preparation = grid.preparation_ns(capture_ns, draw_ns);
        const auto slot_time = [&](uint64_t slot) {
            return start + std::chrono::nanoseconds(grid.swap_offset_ns(slot, preparation));
        };
        const auto first = slot_time(grid.first_slot);
        unsigned extras = 0;
        bool pose_failed = false;
        const auto cutoff = std::chrono::duration_cast<std::chrono::nanoseconds>(
            start + std::chrono::nanoseconds(1'000'000'000 / 30) - first).count();
        const auto elapsed_ns = [&] { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - first).count(); };
        while (const auto frame = schedule.next(std::max<int64_t>(0, elapsed_ns()), cutoff)) {
            draw_started = Clock::now();
            {
                std::lock_guard lock(bank.mutex);
                bank.restore();
                if (!current->apply(bank, reset ? *current : *previous, frame->alpha)) {
                    draw_started.reset();
                    pose_failed = true;
                    break;
                }
            }
            auto* image = bank.data();
            recomp_context ctx;
            copy_guest_context(ctx, caller);
            recomp::ScopedQueueMemory queues(image, rdram, RenderBank::size);
            ExtraRaceDraw extra(image);
            deadline = slot_time(frame->slot);
            RaceSwap display(image, pace);
            func_8000A30C(image, &ctx);
            func_800252D0(image, &ctx);
            fence(rdram);
            draw_started.reset();
            ++extras;
        }
        {
            deadline = slot_time(grid.first_slot + grid.count - 1);
            const auto swap = schedule.native_swap(pose_failed);
            // The original draw must publish a valid pose immediately if an
            // intermediate pose could not be built. A null callback skips it.
            RaceSwap display(rdram, swap == NativeSwap::Immediate ? +[] {} :
                                    swap == NativeSwap::Paced ? pace : nullptr);
            func_8000A30C(rdram, &caller);
            drawn = true;
            recomp_context drain;
            copy_guest_context(drain, caller);
            func_800252D0(rdram, &drain);
            fence(rdram);
        }
        record_group(rate, sample, extras, !previous);
        last_sample = sample;
        previous = std::move(current);
    } catch (const FenceUnavailable&) {
        // A queued action might still reference the bank. Keep its storage
        // alive for the renderer and retire it permanently for this run.
        failed = true;
        end_race_presentation();
        std::fprintf(stderr, "[wdc:render] graphics fence unavailable, retiring render bank\n");
    }
    return drawn;
}

bool draw_at_display_rate(uint8_t* rdram, recomp_context& caller) {
    return drawer.draw(rdram, caller);
}
}

extern "C" const wdc::render::FrameCallbacks* wdc_render_frame_callbacks() {
    using namespace wdc::render;
    static const FrameCallbacks callbacks{race_render_bank().mutex, nullptr, nullptr, diagnostics() ? +[](uint64_t before, uint64_t after) { observe_screen(before, after, explicit_update); } : nullptr,
                                          [] { return explicit_update; }};
    return &callbacks;
}
