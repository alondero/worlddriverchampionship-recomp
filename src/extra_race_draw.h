#pragma once
#include <cstdint>

namespace wdc::render {
// Game-thread scope spanning an owned draw and its guest pipeline drain.
// Matching the RAM pointer lets scheduler calls on live RAM run normally.
class ExtraRaceDraw {
public:
    explicit ExtraRaceDraw(const uint8_t* owned_ram);
    ~ExtraRaceDraw();
    ExtraRaceDraw(const ExtraRaceDraw&) = delete;
    ExtraRaceDraw& operator=(const ExtraRaceDraw&) = delete;
private:
    const uint8_t* previous_;
};
}

extern "C" bool wdc_is_extra_race_draw(const uint8_t* rdram);
