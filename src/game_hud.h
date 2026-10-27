#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wdc::hud {
// BOSS forwards E0 unchanged (ROM DMEM dispatch table -> 0x17DC). It is
// unused by WDC and is consumed by our renderer before reaching RT64.
constexpr uint32_t marker = 0xE0574443;
constexpr uint32_t end_marker = 0xFFFFFFFF;
constexpr uint32_t edge_fill = 0xFFFFFFFE; // begins a menu-art bracket
// Menu frame art fills the original picture. For a texrect at RDP 10.2
// quarter-pixel x [ulx, lrx) with S10.5 s and S5.10 dsdx, return fills from
// that art edge to the output edge in RT64 stretch-rect coordinates, where
// 0..width*4 spans the output. Each repeats the outer texel column (dsdx 0),
// overlapping the art by one pixel to hide rounding and misalignment.
struct Fill { int32_t x0, x1, s; };
struct Fills { unsigned count = 0; Fill fill[2]{}; };
inline Fills edge_fills(int32_t ulx, int32_t lrx, int32_t uls, int32_t dsdx,
                        unsigned width, float scale) {
    Fills out;
    if (!(scale > 1.0f) || !width || lrx <= ulx) return out;
    // A regular rect keeps its proportions about the frame's centre.
    const int32_t right = int32_t(width * 4), inset = 16 * 4; // TV overscan margin
    auto shown = [&](int32_t x) { return width * 2.0f + (x - width * 2.0f) / scale; };
    if (ulx <= inset)
        out.fill[out.count++] = {0, int32_t(std::ceil(shown(ulx))) + 4, uls + dsdx / 64};
    if (lrx >= right - inset)
        out.fill[out.count++] = {int32_t(std::floor(shown(lrx))) - 4, right,
                                 uls + int32_t(int64_t(dsdx) * (lrx - ulx - 2) / 128)};
    return out;
}
inline float travel_scale(float camera_scale, int placement) {
    return placement == 0 ? 0.0f :
        (placement == 1 ? std::min(camera_scale, 4.0f / 3.0f) : camera_scale) - 1.0f;
}
inline float edge_shift(float anchor, float width, float camera_scale, int placement) {
    return (anchor - width / 2.0f) * travel_scale(camera_scale, placement);
}
// RDP texrect after the BOSS E4/E1/F1 rewrite. w0/w1 are the rectangle,
// w2 is the S/T word, and w3 is the 5.10 dsdx/dtdy step.
struct Texrect {
    uint32_t w0 = 0, w1 = 0, w2 = 0, w3 = 0;
    bool operator==(const Texrect&) const = default;
};
// Modes 6 and 8 allocate a buffer twice as tall as the logical picture, and
// RT64 samples that buffer back to the logical height. The H-RES blitters
// still emit the doubled screen rectangle. This undoes that extra Y shift
// for the sprite steps 0x02000100 and 0x02000200 and the digit step 0xFF00.
// Other pictures, including the 480-line menu, are unchanged. It does not
// read or write guest memory.
inline Texrect correct_hres_texrect(Texrect in, int32_t logical_h, int32_t buffer_h) {
    if (logical_h <= 0 || int64_t(buffer_h) != int64_t(logical_h) * 2) return in;
    const unsigned op = (in.w0 >> 24) & 63;
    if (op != 36 && op != 37) return in;
    auto y_of = [](uint32_t word) { return word & 0xFFFu; };
    auto with_y = [](uint32_t word, uint32_t y) { return (word & ~0xFFFu) | (y & 0xFFFu); };
    // func_8006CCD4 with sprite byte +0x1C clear: height doubled, dtdy halved.
    if (in.w3 == 0x02000100u) {
        in.w0 = with_y(in.w0, y_of(in.w0) >> 1);
        in.w1 = with_y(in.w1, y_of(in.w1) >> 1);
        in.w3 = 0x02000200u;
    } else if (in.w3 == 0x02000200u) {
        // Same blitter with byte +0x1C set: the top moved and the height did not.
        const uint32_t top = y_of(in.w1), bottom = y_of(in.w0);
        if (bottom < top) return in;
        const uint32_t height = bottom - top, new_top = top >> 1;
        if (new_top + height > 0xFFFu) return in;
        in.w1 = with_y(in.w1, new_top);
        in.w0 = with_y(in.w0, new_top + height);
    } else if ((in.w3 & 0xFFFFu) == 0xFF00u &&
               ((in.w3 >> 16) == 0x200u || (in.w3 >> 16) == 0x100u)) {
        // func_8002DAD0: one extra Y shift turns the step from 0xFE00 into 0xFF00.
        // The high half is the X step and stays, including mode 8's 0x0100.
        in.w0 = with_y(in.w0, y_of(in.w0) >> 1);
        in.w1 = with_y(in.w1, y_of(in.w1) >> 1);
        in.w3 = (in.w3 & 0xFFFF0000u) | 0xFE00u;
    }
    return in;
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
// 0x8007D75C is a ROM-owned u32 KSEG0 pointer to the active display
// descriptor. +4 is the logical height and +12 is the buffer height, both
// signed 32-bit pixel counts in word-swapped RDRAM. The game thread writes
// the pointer before task submission. The needle hook reads it on the game
// thread, and display-list submission reads it on the graphics worker.
// A null RDRAM pointer, an address outside the 8 MiB window, or one that is
// not 4-byte aligned returns zeros. This read never aborts.
void active_picture_size(uint8_t* rdram, int32_t& logical_h, int32_t& buffer_h);
}
