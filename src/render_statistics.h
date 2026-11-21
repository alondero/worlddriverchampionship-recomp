#pragma once
#include "render_cadence.h"

namespace wdc::render {
bool diagnostics();
void observe_screen(uint64_t before, uint64_t after, bool explicit_update);
void record_group(DisplayRate rate, unsigned sample, unsigned extras, bool first_group);
}
