// Host-only rational presentation cadence. No guest clock or memory access.
#pragma once
#include <cstdint>
namespace prototype {
constexpr unsigned presentations_in_group(uint64_t group, unsigned hz) {
    return unsigned(((group + 1) * hz) / 30 - (group * hz) / 30);
}
// draw is one-based within this zero-based nominal 30 Hz group.
constexpr double presentation_alpha(uint64_t group, unsigned draw, unsigned hz) {
    const uint64_t presentation = group * hz / 30 + draw;
    return double(presentation * 30 - group * hz) / hz;
}
// A bounded suffix of the display grid inside an actual snapshot interval.
// Callers provide a positive sample span and a positive rate/budget.
struct SampleSchedule {
    uint64_t begin, end, first_slot;
    unsigned count, skipped, hz;
    double alpha(unsigned draw) const {
        return double((first_slot + draw - 1) * 30 - begin * hz) / ((end - begin) * hz);
    }
};
constexpr SampleSchedule schedule_samples(uint64_t begin, uint64_t end, unsigned hz, unsigned budget) {
    const uint64_t first = begin * hz / 30 + 1;
    const uint64_t last = end * hz / 30;
    const unsigned available = unsigned(last - first + 1);
    const unsigned count = available < budget ? available : budget;
    return {begin, end, last - count + 1, count, available - count, hz};
}
// Select before rendering, never repeat an earlier slot, and always retain
// the final slot. Elapsed time is relative to the beginning of this draw group.
constexpr unsigned due_slot(unsigned next, unsigned count, uint64_t elapsed_ns, unsigned hz) {
    const uint64_t due = elapsed_ns * hz / 1000000000;
    const uint64_t selected = due > next ? due : next;
    return unsigned(selected < count ? selected : count - 1);
}
class PresentationPeriod {
    unsigned hz, remainder = 0;
public:
    explicit PresentationPeriod(unsigned rate) : hz(rate) {}
    uint64_t next_ns() {
        uint64_t ns = 1000000000 / hz;
        remainder += 1000000000 % hz;
        if (remainder >= hz) { remainder -= hz; ++ns; }
        return ns;
    }
};
}
