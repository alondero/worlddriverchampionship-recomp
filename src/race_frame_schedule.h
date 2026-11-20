#pragma once

#include "render_cadence.h"
#include <optional>

namespace wdc::render {
struct RacePresentation {
    uint64_t slot;
    double alpha;
    unsigned index;
    unsigned skipped;
};

// Game-thread scheduler for a single race view. Input gaps are measured in
// original 30 Hz samples. Deadlines are relative to the start of this group.
class RaceFrameSchedule {
public:
    [[nodiscard]] bool begin_group(DisplayRate rate, unsigned sample_gap, bool discontinuity, bool reuse_endpoint);
    std::optional<RacePresentation> next(uint64_t elapsed_ns = 0, uint64_t native_deadline_ns = UINT64_MAX);
    const SampleSchedule& group() const { return group_; }
    bool present_native() const { return native_; }

private:
    SampleSchedule group_{};
    unsigned next_ = 0, owned_ = 0;
    bool native_ = false;
};
}
