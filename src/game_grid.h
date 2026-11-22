#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wdc::grid {
inline constexpr uint32_t original = UINT32_MAX;
inline constexpr uint32_t car_count = 35;
inline constexpr uint32_t color_count = 3;
inline constexpr std::size_t opponent_count = 7;

struct Opponent {
    uint32_t car = original;
    uint32_t color = original;
};

// Ordered from the front of the starting grid, skipping the player.
using Grid = std::array<Opponent, opponent_count>;

// Publish the complete grid between the frontend and race loader.
bool set(const Grid& grid);
}

extern "C" void wdc_apply_custom_grid(uint8_t* rdram);
