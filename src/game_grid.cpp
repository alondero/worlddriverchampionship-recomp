#include "game_grid.h"
#include "recomp.h"
#include <mutex>

namespace {
std::mutex grid_mutex;
wdc::grid::Grid custom_grid;
}

bool wdc::grid::set(const Grid& grid) {
    for (const auto& opponent : grid) {
        if ((opponent.car != original && opponent.car >= car_count) ||
            (opponent.color != original && opponent.color >= color_count)) return false;
    }
    std::lock_guard lock(grid_mutex);
    custom_grid = grid;
    return true;
}

// USA race config in word-swapped 8 MiB RDRAM. The game dispatcher calls this
// at 800369E0 after setup/restart and the warp hook, before 80037034/38 reads
// model and palette words for the vehicle builder. All fields are big-endian
// u32 via MEM_W: counts at +0/+4, eight model IDs at +8, palette IDs at +28,
// zero-based start positions at +48, mode at +84. Slot 0 is the sole human in
// modes 2 and 3. The original restart snapshot stays a source of game defaults.
// Invalid counts/positions skip the entire override. No guest state is retained
// in the host. Replace this bridge with the named race setup when recovered.
extern "C" void wdc_apply_custom_grid(uint8_t* rdram) {
    if (!rdram) return;
    constexpr int32_t config = int32_t(0x80092360);
    const auto mode = MEM_W(0x84, config);
    const int count = MEM_W(4, config);
    if ((mode != 2 && mode != 3) || MEM_W(0, config) != 1 || count < 2 || count > 8) return;

    std::array<int, 8> slot_by_position{};
    unsigned positions_seen = 0;
    for (int slot = 0; slot < count; ++slot) {
        const int position = MEM_W(0x48 + slot * 4, config);
        if (position < 0 || position >= count || (positions_seen & (1u << position))) return;
        positions_seen |= 1u << position;
        slot_by_position[position] = slot;
    }

    wdc::grid::Grid grid;
    {
        std::lock_guard lock(grid_mutex);
        grid = custom_grid;
    }
    unsigned opponent = 0;
    for (int position = 0; position < count; ++position) {
        const auto slot = slot_by_position[position];
        if (slot == 0) continue;
        const auto [car, color] = grid[opponent++];
        if (car != wdc::grid::original) MEM_W(8 + slot * 4, config) = car;
        if (color != wdc::grid::original) MEM_W(0x28 + slot * 4, config) = color;
    }
}
