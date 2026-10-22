#pragma once
#include <algorithm>
#include <cstdint>

namespace wdc::hud {
// BOSS forwards E0 unchanged (ROM DMEM dispatch table -> 0x17DC). It is
// unused by WDC and is consumed by our renderer before reaching RT64.
constexpr uint32_t marker = 0xE0574443;
constexpr uint32_t end_marker = 0xFFFFFFFF;
inline float travel_scale(float camera_scale, int placement) {
    return placement == 0 ? 0.0f :
        (placement == 1 ? std::min(camera_scale, 4.0f / 3.0f) : camera_scale) - 1.0f;
}
inline float edge_shift(float anchor, float width, float camera_scale, int placement) {
    return (anchor - width / 2.0f) * travel_scale(camera_scale, placement);
}
// Command lengths in 64-bit words. Track continuation words across BOSS DMA
// chunks: a texture coefficient that resembles a marker is still payload.
inline unsigned command_words(uint32_t word) {
    const unsigned op = (word >> 24) & 63;
    if (op >= 8 && op <= 15)
        return 4 + ((op & 4) ? 8 : 0) + ((op & 2) ? 8 : 0) + ((op & 1) ? 2 : 0);
    return op == 36 || op == 37 ? 2 : 1;
}
struct Stream {
    unsigned remaining = 0;
    bool consume(uint32_t w0) {
        if (remaining) { --remaining; return false; }
        remaining = command_words(w0) - 1;
        return w0 == marker;
    }
};
void set_placement(int placement);
float needle_shift(float x, float width, float scale);
}
