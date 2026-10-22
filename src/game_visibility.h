#pragma once

namespace wdc::visibility {
// SDL publishes host policy; hooks consume it on the guest render thread.
// Defaults preserve ROM behavior, including in the headless diagnostic.
void configure(bool full_cars, bool full_track, int distance);
}
