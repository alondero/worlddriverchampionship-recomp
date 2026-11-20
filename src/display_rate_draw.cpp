#include "display_rate_draw.h"
#include "extra_race_draw.h"
#include "guest_context.h"
#include "graphics_config.h"
#include "race_frame_schedule.h"
#include "race_poses.h"
#include "race_render_sample.h"
#include "race_swap.h"
#include "render_memory.h"
#include "savestate.h"
#include "librecomp/queue_memory.hpp"
#include "ultramodern/swap_presentation.hpp"
#include "ultramodern/ultramodern.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
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
    const unsigned hz = settings.rate == -1 ? ultramodern::get_display_refresh_rate() : unsigned(settings.rate);
    return {hz > 30 ? std::min(hz, DisplayRate::max_hz) : 0, 1};
}

// Renderer worker TLS is bound to each explicit screen action by the runtime.
thread_local bool explicit_update = false;
std::atomic<unsigned> explicit_frames{}, retrace_frames{};
bool diagnostics() {
    static const bool enabled = std::getenv("WDC_DIAGNOSTIC") || std::getenv("WDC_RENDER_STATS");
    return enabled;
}
void observe_screen(uint64_t before, uint64_t after) {
    if (before != after) (explicit_update ? explicit_frames : retrace_frames).fetch_add(1, std::memory_order_relaxed);
}
thread_local bool presenting = false;
thread_local std::optional<RacePoses> previous;
thread_local std::optional<uint32_t> last_sample;
void set_presenting(bool enabled) {
    if (presenting == enabled) return;
    ultramodern::set_swap_presentation(enabled, {[](bool active) { explicit_update = active; }});
    presenting = enabled;
}

// Game-thread state only. Fences can yield to guest workers on other threads.
thread_local uint8_t* live_image = nullptr;
thread_local Clock::time_point deadline{};
thread_local DisplayRate active_rate{};
thread_local std::optional<PresentationPeriod> period;
void pace() {
    auto now = Clock::now();
    if (now > deadline + std::chrono::milliseconds(34)) deadline = now;
    while (now < deadline) {
        ultramodern::wait_for_external_message_timed(live_image, 1);
        now = Clock::now();
    }
    deadline += std::chrono::nanoseconds(period->next_ns());
}

struct FenceUnavailable {};
void fence(uint8_t* live) {
    if (!ultramodern::wait_for_submitted_graphics(live)) throw FenceUnavailable{};
}

bool eligible(uint8_t* rdram) {
    // Fixed, aligned USA u32 game globals in the word-swapped 8 MiB image.
    // Read on the dispatcher thread after simulation and before drawing.
    const auto word = [rdram](uint32_t address) { return uint32_t(MEM_W(0, int32_t(address))); };
    const auto scene = word(0x800923E4);
    return word(0x8007D848) == 0x8000C560 && !word(0x8007DF44) &&
           !word(0x8007D794) && word(0x8007E580) == 1 &&
           word(0x80080D90) && word(0x8007FE1C) == 1 &&
           word(0x8007FDF0) == 0 && word(0x8007FDF4) == 1562500 &&
           (scene == 0 || scene == 3 || scene == 5 || scene == 8 || scene == 9);
}
void end_race_presentation() {
    set_presenting(false);
    previous.reset();
    last_sample.reset();
    deadline = {};
}
}

// USA 80001D90, same dispatcher thread, after any checkpoint restore and
// before the indirect game/menu/loader callback. Keep retrace presentation
// disabled between active draws, including fractional groups without a native
// endpoint. Release it before handing the display back to another scene.
extern "C" void wdc_render_dispatcher_tick(uint8_t* rdram) {
    if (!presenting) return;
    static thread_local uint64_t generation = 0;
    const auto restored = state::restore_generation();
    // The ROM can hold nesting counter 8007D858 during ordinary races.
    // The draw site checks whether gameplay actually advanced.
    if (restored != generation || uint32_t(MEM_W(0, int32_t(0x8007D848))) != 0x8000C560 ||
        MEM_W(0, int32_t(0x8007DF44))) end_race_presentation();
    generation = restored;
}

bool draw_at_display_rate(uint8_t* rdram, recomp_context& caller) {
    const auto rate = requested_rate();
    static thread_local bool failed = false;
    if (!rate.numerator || failed) {
        if (presenting) end_race_presentation();
        return false;
    }
    if (active_rate.numerator != rate.numerator || active_rate.denominator != rate.denominator) {
        end_race_presentation();
        active_rate = rate;
        period.emplace(rate);
    }
    static thread_local RaceFrameSchedule schedule;
    if (!eligible(rdram)) {
        end_race_presentation();
        return false;
    }

    const uint32_t sample = MEM_W(0, int32_t(0x8007E530));
    if (last_sample == sample) {
        end_race_presentation();
        last_sample = sample;
        return false;
    }
    const auto start = Clock::now();
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
        schedule.begin_group(rate, continuity.sample_gap, reset, true);
        unsigned extras = 0;
        bool pose_failed = false;
        constexpr uint64_t tick_ns = 1'000'000'000 / 30;
        const auto elapsed_ns = [&] { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count(); };
        while (const auto frame = schedule.next(elapsed_ns(), tick_ns)) {
            {
                std::lock_guard lock(bank.mutex);
                bank.restore();
                if (!current->apply(bank, reset ? *current : *previous, frame->alpha)) {
                    pose_failed = true;
                    break;
                }
            }
            auto* image = bank.data();
            recomp_context ctx;
            copy_guest_context(ctx, caller);
            recomp::ScopedQueueMemory queues(image, rdram, RenderBank::size);
            ExtraRaceDraw extra(image);
            RaceSwap display(image, pace);
            func_8000A30C(image, &ctx);
            func_800252D0(image, &ctx);
            fence(rdram);
            ++extras;
        }
        {
            RaceSwap display(rdram, (schedule.present_native() || pose_failed) ? pace : nullptr);
            func_8000A30C(rdram, &caller);
            drawn = true;
            recomp_context drain;
            copy_guest_context(drain, caller);
            func_800252D0(rdram, &drain);
            fence(rdram);
        }
        if (diagnostics()) {
            static thread_local unsigned groups, first_explicit, first_retrace, first_sample;
            static thread_local Clock::time_point first;
            if (!previous) {
                std::fprintf(stderr, "[wdc:render] fixed 30 Hz gameplay, display target %u/%u Hz, %u intermediate draws in first group\n",
                             rate.numerator, rate.denominator, extras);
                groups = 0;
            }
            if (groups == 60) {
                const auto seconds = std::chrono::duration<double>(Clock::now() - first).count();
                std::fprintf(stderr, "[wdc:render] 60 groups, %.3f s, %u samples, RT64 accepted %u explicit and %u retrace frames\n",
                             seconds, sample - first_sample, explicit_frames.load() - first_explicit,
                             retrace_frames.load() - first_retrace);
                groups = 0;
            }
            if (groups++ == 0) {
                first = Clock::now();
                first_sample = sample;
                first_explicit = explicit_frames.load();
                first_retrace = retrace_frames.load();
            }
        }
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
}

extern "C" const wdc::render::FrameCallbacks* wdc_render_frame_callbacks() {
    using namespace wdc::render;
    static const FrameCallbacks callbacks{race_render_bank().mutex, nullptr, nullptr, diagnostics() ? observe_screen : nullptr,
                                          [] { return explicit_update; }};
    return &callbacks;
}
