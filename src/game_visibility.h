#pragma once

#include <cstdint>

namespace wdc::visibility {
// SDL publishes host policy; hooks consume it on the guest render thread.
// Defaults preserve ROM behavior, including in the headless diagnostic.
// Distance 16 is the UI's bounded Unlimited mode; projection still needs a
// finite matrix, so it uses a large 64x practical range rather than infinity.
inline constexpr int unlimited_draw_distance = 16;
inline constexpr float unlimited_draw_distance_scale = 64.0f;
inline constexpr int max_track_sections = 1024;
inline constexpr uint32_t track_section_list = 0x807F2880u; // rom-symbols.toml [visibility]
void configure(bool full_cars, bool full_track, int distance);
float distance_scale();
}
