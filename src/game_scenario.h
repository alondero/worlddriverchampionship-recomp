#pragma once
#include <cstdint>

namespace wdc::scenario {
bool configure(); // Before runtime threads start.
bool enabled();
bool read_input(int port, uint16_t* buttons, float* x, float* y);
void dispatcher_tick(const uint8_t* rdram);
void vi_tick(unsigned vi);
void graphics_task();
void presentation();
}
