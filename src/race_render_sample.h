#pragma once
#include "recomp.h"
#include "render_bank.h"

namespace wdc::render {
// Game-thread race draw boundary. rdram is the live, word-swapped 8 MiB game
// image. wait_renderer drains host task and screen reads after the ROM has
// retired its pending transform batch. The caller retains its register state.
RenderBank::Transfer capture_race_sample(uint8_t* rdram, const recomp_context& caller,
                                         bool prefer_write_watch, void (*wait_renderer)(uint8_t*));
}
