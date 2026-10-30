// Argument parsing for wdc_boss_bench.
//
// The bench is a command-line tool that needs a capture file to do anything,
// so its argument handling is kept here where a host test can check it
// without one. The bench counts one timed iteration per replay and the
// count has to be bounded: the loop counts up to the limit, so a count near
// the type's range would wrap the counter and never end.
#pragma once

#include <string>

namespace wdc::boss_bench {

// The largest iteration count the bench accepts. At roughly 2 ms per
// iteration this is still a run of half an hour, which is far past any
// useful measurement.
inline constexpr unsigned kMaxIterations = 1000000;

// Parses a plain decimal count. Returns false for an empty string, for
// anything that is not a decimal digit, for a count of zero (a bench that
// runs nothing is a usage error), and for a count above kMaxIterations.
inline bool parse_iterations(const std::string& text, unsigned& iterations) {
    if (text.empty()) return false;
    unsigned long long value = 0;
    for (const char digit : text) {
        if (digit < '0' || digit > '9') return false;
        value = value * 10 + unsigned(digit - '0');
        // Checked on every digit, so the accumulator cannot overflow.
        if (value > kMaxIterations) return false;
    }
    if (value == 0) return false;
    iterations = static_cast<unsigned>(value);
    return true;
}

} // namespace wdc::boss_bench
