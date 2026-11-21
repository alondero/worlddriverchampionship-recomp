#include "extra_race_draw.h"

namespace {
thread_local const uint8_t* drawing_bank = nullptr;
}

wdc::render::ExtraRaceDraw::ExtraRaceDraw(const uint8_t* owned_ram) : previous_(drawing_bank) {
    drawing_bank = owned_ram;
}

wdc::render::ExtraRaceDraw::~ExtraRaceDraw() {
    drawing_bank = previous_;
}

extern "C" bool wdc_is_extra_race_draw(const uint8_t* rdram) {
    return drawing_bank && drawing_bank == rdram;
}
