#include "render_statistics.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace wdc::render {
using Clock = std::chrono::steady_clock;
static std::atomic<unsigned> explicit_frames{}, retrace_frames{};
bool diagnostics() {
    static const bool enabled = std::getenv("WDC_DIAGNOSTIC") || std::getenv("WDC_RENDER_STATS");
    return enabled;
}
void observe_screen(uint64_t before, uint64_t after, bool explicit_update) {
    if (before == after) return;
    (explicit_update ? explicit_frames : retrace_frames).fetch_add(1, std::memory_order_relaxed);
    static thread_local std::optional<bool> last_owner;
    if (last_owner != explicit_update) {
        last_owner = explicit_update;
        std::fprintf(stderr, "[wdc:render] RT64 accepted presentation owner=%s\n",
                     explicit_update ? "swap" : "retrace");
    }
}
void record_release(const char* reason) {
    if (diagnostics()) std::fprintf(stderr, "[wdc:render] releasing swap presentation: %s\n", reason);
}
void record_group(DisplayRate rate, unsigned sample, unsigned extras, bool first_group) {
    if (diagnostics()) {
        static thread_local unsigned groups, first_explicit, first_retrace, first_sample;
        static thread_local Clock::time_point first;
        if (first_group) {
            std::fprintf(stderr, "[wdc:render] fixed 30 Hz gameplay, display target %u/%u Hz, %u intermediate draws in first group (sample=%u)\n",
                         rate.numerator, rate.denominator, extras, sample);
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
}
}
