#pragma once

namespace wdc::framerate {
// SDL thread publishes the saved preference. The game latches it at the next
// input-worker startup, before the ROM initializes its divider and timestep.
void configure(bool native_60fps);
}
