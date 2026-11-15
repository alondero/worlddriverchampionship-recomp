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
