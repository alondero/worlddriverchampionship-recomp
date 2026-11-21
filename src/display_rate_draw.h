#pragma once
#include "recomp.h"

namespace wdc::render {
// Returns true when this invocation supplied the authoritative race draw.
bool draw_at_display_rate(uint8_t* rdram, recomp_context& caller);
}
