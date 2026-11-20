#pragma once

#include "render_cadence.h"
#include <chrono>
#include <optional>

namespace wdc::render {
enum class NativeSwap { Skip, Paced, Immediate };

struct RacePresentation {
    uint64_t slot;
    double alpha;
    unsigned index;
    unsigned skipped;
};

// Only a private draw arms the estimate. Its first swap completes the
// measurement before pacing. Other swaps have no effect.
class DrawTimeEstimate {
public:
    using Time = std::chrono::steady_clock::time_point;
    void begin_private_draw(Time now) { started_ = now; }
    void finish_private_draw_at_swap(Time now) {
        if (started_) {
            last_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(now - *started_).count();
            end_private_draw();
        }
    }
    void end_private_draw() { started_.reset(); }
    int64_t value_ns() const { return last_ns_; }

private:
    std::optional<Time> started_;
    int64_t last_ns_ = 0;
};

// Game-thread scheduler for a single race view. Input gaps are measured in
// original 30 Hz samples. Deadlines are relative to the start of this group.
class RaceFrameSchedule {
public:
    [[nodiscard]] bool begin_group(DisplayRate rate, unsigned sample_gap, bool discontinuity, bool reuse_endpoint);
    std::optional<RacePresentation> next(uint64_t elapsed_ns = 0, uint64_t native_deadline_ns = UINT64_MAX);
    void begin_private_draw(std::chrono::steady_clock::time_point now) { draw_time_.begin_private_draw(now); }
    void finish_private_draw_at_swap(std::chrono::steady_clock::time_point now) { draw_time_.finish_private_draw_at_swap(now); }
    void end_private_draw() { draw_time_.end_private_draw(); }
    int64_t preparation_ns(int64_t capture_ns) const {
        return group_.preparation_ns(capture_ns, draw_time_.value_ns());
    }
    const SampleSchedule& group() const { return group_; }
    NativeSwap native_swap(bool pose_failed) const {
        if (pose_failed) return NativeSwap::Immediate;
        return native_ ? NativeSwap::Paced : NativeSwap::Skip;
    }
    bool present_native() const { return native_; }

private:
    DrawTimeEstimate draw_time_;
    SampleSchedule group_{};
    unsigned next_ = 0, owned_ = 0;
    bool native_ = false;
};
}
