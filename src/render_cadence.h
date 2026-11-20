// Host-only rational presentation cadence. No guest clock or memory access.
#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>

namespace wdc::render {

// Ratio in frames/second. Callers validate 30 < Hz <= 1000 and a
// denominator up to one million before constructing the grid.
struct DisplayRate {
    // Match RT64's upper bound for a custom refresh target.
    static constexpr unsigned max_hz = 1000;
    unsigned numerator, denominator;
    static constexpr DisplayRate from_millihertz(unsigned value) {
        const auto divisor = std::gcd(value, 1000u);
        return {value / divisor, 1000u / divisor};
    }
    constexpr bool supported() const {
        return denominator && denominator <= 1000000 && numerator > sample_scale() &&
               numerator <= uint64_t(max_hz) * denominator;
    }
    constexpr uint64_t sample_scale() const { return 30ull * denominator; }
    constexpr unsigned draw_budget() const {
        return unsigned((numerator + sample_scale() - 1) / sample_scale());
    }
    constexpr uint64_t last_slot(uint64_t sample) const {
        return sample * numerator / sample_scale();
    }
};

constexpr unsigned presentations_in_group(uint64_t group, DisplayRate rate) {
    return unsigned(rate.last_slot(group + 1) - rate.last_slot(group));
}

// draw is one-based within this zero-based nominal 30 Hz group.
constexpr double presentation_alpha(uint64_t group, unsigned draw, DisplayRate rate) {
    const uint64_t presentation = rate.last_slot(group) + draw;
    return double(presentation * rate.sample_scale() - group * rate.numerator) / rate.numerator;
}

// A bounded suffix of the display grid inside an actual snapshot interval.
// Callers provide a positive sample span and a positive rate/budget.
struct SampleSchedule {
    uint64_t begin, end, first_slot;
    unsigned count, skipped;
    DisplayRate rate;
    constexpr bool ends_on_grid() const {
        return end * rate.numerator % rate.sample_scale() == 0;
    }
    // Reserve the measured time to capture this sample and build a draw.
    // At most one slot of work can be admitted ahead of the current pose.
    int64_t preparation_ns(int64_t capture, int64_t draw) const {
        const int64_t period = 1'000'000'000ll * rate.denominator / rate.numerator;
        return std::min(period, capture + draw);
    }
    int64_t swap_offset_ns(uint64_t slot, int64_t preparation) const {
        const auto scale = rate.sample_scale();
        const auto tick = end - 1;
        const int64_t slots = int64_t(slot - 1) - int64_t(rate.last_slot(tick));
        const int64_t phase = int64_t(tick * rate.numerator % scale);
        return preparation + (slots * int64_t(scale) - phase) * 1'000'000'000 / (30ll * rate.numerator);
    }
    double alpha(unsigned draw) const {
        const uint64_t offset = (first_slot + draw - 1) * rate.sample_scale() - begin * rate.numerator;
        return double(offset) / ((end - begin) * rate.numerator);
    }
};

constexpr SampleSchedule schedule_samples(uint64_t begin, uint64_t end, DisplayRate rate, unsigned budget) {
    const uint64_t first = rate.last_slot(begin) + 1;
    const uint64_t last = rate.last_slot(end);
    const unsigned available = unsigned(last - first + 1);
    const unsigned count = available < budget ? available : budget;
    return {begin, end, last - count + 1, count, available - count, rate};
}

// Select before rendering, never repeat an earlier slot, and always retain
// the final slot. Elapsed time is relative to the beginning of this draw group.
constexpr unsigned due_slot(unsigned next, unsigned count, uint64_t elapsed_ns, DisplayRate rate) {
    const uint64_t period_scale = 1000000000ull * rate.denominator;
    // Bound the multiplication below after long stalls. A group has at most
    // 34 slots at 1000 Hz with the stock 30 Hz simulation.
    if (elapsed_ns > count * period_scale / rate.numerator) return count - 1;
    const uint64_t due = elapsed_ns * rate.numerator / period_scale;
    const uint64_t selected = due > next ? due : next;
    return unsigned(selected < count ? selected : count - 1);
}

class PresentationPeriod {
    DisplayRate rate;
    unsigned remainder = 0;
public:
    explicit PresentationPeriod(DisplayRate rate) : rate(rate) {}
    uint64_t next_ns() {
        const uint64_t scale = 1000000000ull * rate.denominator;
        uint64_t ns = scale / rate.numerator;
        remainder += unsigned(scale % rate.numerator);
        if (remainder >= rate.numerator) {
            remainder -= rate.numerator;
            ++ns;
        }
        return ns;
    }
};

} // namespace wdc::render
