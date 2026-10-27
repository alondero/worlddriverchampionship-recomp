#pragma once
#include "hle/rt64_vi.h"
#include <array>

namespace wdc::test {
// Runs the pinned presenter's geometry with a size-only swapchain boundary.
std::array<float, 4> vi_viewport(const RT64::VI& vi, int width, int height,
                               float scale_x, float scale_y, bool remove_borders = false);
}
