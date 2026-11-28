#pragma once
#include <cstdint>
#include <optional>

namespace wdc::scenario {
bool configure(); // Before runtime threads start.
bool enabled();
bool read_input(int port, uint16_t* buttons, float* x, float* y);
// SDL main thread only. Deliver the scripted setting once, during a race.
std::optional<int> take_draw_distance_change();
void dispatcher_tick(const uint8_t* rdram);
void vi_tick(unsigned vi);
void graphics_task();
void presentation();
}
