// Host-only rational presentation cadence. No guest clock or memory access.
#pragma once
#include <cstdint>
namespace prototype {
constexpr unsigned presentations_in_group(uint64_t group, unsigned hz) {
    return unsigned(((group + 1) * hz) / 30 - (group * hz) / 30);
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
