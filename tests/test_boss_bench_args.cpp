// The bench's argument handling, without a capture file.
//
// wdc_boss_bench needs a .boss capture to do anything, so its argument
// parsing lives in tools/boss/boss_bench_args.h and is checked here. The
// count of timed iterations is the one argument that can be hostile: a
// count of zero or a negative number would make the bench do nothing or
// run for ever.
#include "boss_bench_args.h"

#include <cstdio>
#include <string>

namespace {

unsigned accepted(const std::string& text) {
    unsigned iterations = 0;
    if (!wdc::boss_bench::parse_iterations(text, iterations)) {
        std::fprintf(stderr, "unexpectedly accepted %s\n", text.c_str());
        std::exit(1);
    }
    return iterations;
}

bool rejected(const std::string& text) {
    unsigned iterations = 12345;
    return !wdc::boss_bench::parse_iterations(text, iterations);
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

} // namespace

int main() {
    require(accepted("200") == 200, "a plain count was not accepted");
    require(accepted("1") == 1, "a count of one was not accepted");
    require(accepted("0005") == 5, "leading zeros were not accepted");
    require(accepted("1000000") == wdc::boss_bench::kMaxIterations,
        "the largest allowed count was rejected");

    require(rejected(""), "an empty count was accepted");
    require(rejected("0"), "a count of zero was accepted");
    require(rejected("-1"), "a negative count was accepted");
    require(rejected("+1"), "a signed count was accepted");
    require(rejected(" 5"), "a count with a space was accepted");
    require(rejected("5 "), "a count with a trailing space was accepted");
    require(rejected("1e3"), "a count in exponent form was accepted");
    require(rejected("0x10"), "a count in hexadecimal was accepted");
    require(rejected("abc"), "a non-numeric count was accepted");
    // The runaway the guard exists for: the count would wrap the loop's
    // counter and never end.
    require(rejected("4294967295"), "a count at the unsigned limit was accepted");
    require(rejected("1000001"), "a count above the limit was accepted");
    require(rejected("99999999999999999999999"), "an enormous count was accepted");

    std::puts("BOSS bench arguments passed");
}
