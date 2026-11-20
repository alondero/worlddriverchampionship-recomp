#pragma once
#include <cstdint>
#include <cstring>

namespace wdc::render::race {
// USA game-owned aligned u32 globals, all within KSEG0's first 8 MiB.
// Sampled by the dispatcher thread between physics and draw. Frozen pose reads
// use the same addresses in the private snapshot. N64 big-endian words are
// native u32 in word-swapped RDRAM. No guest pointer escapes the draw.
// A missing image or unrecognised state returns to ordinary presentation.
// Replace these reads when the recovered dispatcher exposes a typed race view.
inline constexpr uint32_t scene = 0x800923E4;       // Race setup mode ID.
inline constexpr uint32_t callback = 0x8007D848;    // Dispatcher function VA.
inline constexpr uint32_t fade = 0x8007DF44;        // Transition request flag.
inline constexpr uint32_t mode_gate = 0x8007D794;   // Nonzero excludes active gameplay at 8004D52C.
inline constexpr uint32_t views = 0x8007E580;       // Number of active views.
inline constexpr uint32_t cars = 0x80080D90;        // Race roster length in cars.
inline constexpr uint32_t divider = 0x8007FE1C;     // Input worker VI poll interval minus one.
inline constexpr uint32_t step_hi = 0x8007FDF0;     // Startup fixed step, high word of OS ticks.
inline constexpr uint32_t step_lo = 0x8007FDF4;     // Low word, 46.875 MHz OS clock.
inline constexpr uint32_t sample = 0x8007E530;      // Number of consumed simulation samples.
inline constexpr uint32_t race_callback = 0x8000C560;

inline uint32_t word(const uint8_t* image, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, image + (address - 0x80000000u), sizeof(value));
    return value;
}
inline bool active(const uint8_t* image) {
    return word(image, callback) == race_callback && !word(image, fade);
}
inline bool eligible(const uint8_t* image) {
    const auto mode = word(image, scene);
    // Quick Race arcade/time attack/practice are 3/5/8. The race dispatcher
    // also accepts observed single-view modes 0/9. Unknown and demo IDs fall back.
    return active(image) && !word(image, mode_gate) && word(image, views) == 1 && word(image, cars) &&
           word(image, divider) == 1 && !word(image, step_hi) && word(image, step_lo) == 1562500 &&
           (mode == 0 || mode == 3 || mode == 5 || mode == 8 || mode == 9);
}

class DispatcherHistory {
    uint64_t generation = 0;
public:
    bool restored_while_presenting(bool presenting, uint64_t current) {
        const bool changed = current != generation;
        generation = current;
        return presenting && changed;
    }
};
}
