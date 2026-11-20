#include "race_frame_schedule.h"

namespace wdc::render {
bool RaceFrameSchedule::begin_group(DisplayRate rate, unsigned sample_gap, bool discontinuity, bool reuse_endpoint) {
    next_ = owned_ = 0;
    native_ = false;
    if (!rate.supported()) {
        draw_time_ = {};
        group_ = {};
        return false;
    }

    // Re-anchor when poses cannot be blended across the boundary or the
    // display grid changes. Use one stationary sample interval on that frame.
    const bool reset = discontinuity || !sample_gap || sample_gap > 4 ||
                       group_.rate.numerator != rate.numerator || group_.rate.denominator != rate.denominator;
    if (reset) draw_time_ = {};
    const uint64_t begin = reset ? 0 : group_.end;
    group_ = schedule_samples(begin, begin + (reset ? 1 : sample_gap), rate, rate.draw_budget());
    native_ = reuse_endpoint && group_.ends_on_grid();
    owned_ = group_.count - unsigned(native_);
    return true;
}

std::optional<RacePresentation> RaceFrameSchedule::next(uint64_t elapsed_ns, uint64_t native_deadline_ns) {
    if (next_ >= owned_) return std::nullopt;
    // The next fixed simulation tick is due. The authoritative draw still
    // runs the original game and audio work, and can present its current
    // pose even if this fractional group normally ends between display slots.
    if (native_deadline_ns != UINT64_MAX && elapsed_ns >= native_deadline_ns) {
        next_ = owned_;
        native_ = true;
        return std::nullopt;
    }
    const unsigned index = due_slot(next_, owned_, elapsed_ns, group_.rate);
    const unsigned skipped = index - next_;
    next_ = index + 1;
    return RacePresentation{group_.first_slot + index, group_.alpha(index + 1), index, skipped};
}
}
