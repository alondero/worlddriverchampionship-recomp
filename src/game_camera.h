#pragma once

namespace wdc::camera {
// SDL publishes the drawable aspect; the guest camera and renderer read it.
// Headless startup uses the original TV aspect. Narrow windows keep 4:3.
void set_output_size(int width, int height);
float width_scale();
}
