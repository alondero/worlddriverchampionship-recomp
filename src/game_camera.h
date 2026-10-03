#pragma once

namespace wdc::camera {
// SDL publishes the window aspect; the guest camera and renderer read it.
// Headless startup and non-positive dimensions use the original TV aspect.
// Narrow windows keep 4:3.
void set_output_size(int width, int height);
float width_scale();
}
