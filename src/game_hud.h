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
// for a texrect whose step proves the blitter doubled Y: sprite step
// 0x02000100 on opcode 36 or 37, or a digit low half of 0xFF00 on opcode 36.
// Step 0x02000200 is both the flag-clear rectangle and the sprite byte +0x1C
// packet, so it does not prove that the height doubled. Opcode 37 is flipped:
// RT64 applies dsdx along screen Y and dtdy along screen X. func_8006CA7C
// leaves dsdx at 0x200 and halves dtdy, so restoring that low half after the
// screen Y is halved matches the flag-clear packet on both axes. The digit
// low half stays opcode 36, because on a flip it would change the X axis.
// Other pictures, including the 480-line menu, are unchanged. It does not
// read or write guest memory.
inline Texrect correct_hres_texrect(Texrect in, int32_t logical_h, int32_t buffer_h) {
    if (logical_h <= 0 || int64_t(buffer_h) != int64_t(logical_h) * 2) return in;
    const unsigned op = (in.w0 >> 24) & 63;
    if (op != 36 && op != 37) return in;
    auto y_of = [](uint32_t word) { return word & 0xFFFu; };
    auto with_y = [](uint32_t word, uint32_t y) { return (word & ~0xFFFu) | (y & 0xFFFu); };
    // Height-doubled sprite: func_8006DA18, func_8006D22C, func_8006CCD4
    // and the flipped pause edge func_8006CA7C, with the H-RES flag set and
    // sprite byte +0x1C clear. The stored step is 0x02000100.
    if (in.w3 == 0x02000100u) {
        in.w0 = with_y(in.w0, y_of(in.w0) >> 1);
        in.w1 = with_y(in.w1, y_of(in.w1) >> 1);
        in.w3 = 0x02000200u;
    } else if (op == 36 && (in.w3 & 0xFFFFu) == 0xFF00u &&
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
// Display-list framing for one task. The graphics worker owns this. Guest
// memory is not modified. `hold` is the first 8 bytes of a texrect whose
// coefficient is in a later BOSS chunk.
struct RdpTask {
    Stream stream{};
    bool fill_active = false;
    bool fill_pending = false;
    uint32_t fill_w0 = 0, fill_w1 = 0;
    bool hold = false;
    uint32_t hold_w0 = 0, hold_w1 = 0;
};
// Walk one BOSS chunk of word-swapped RDP bytes. `memory` may be RDRAM or
// DMEM; the picture size is supplied by the caller, which read it from
// RDRAM. The sink observes what RT64 would be given:
//   guest(begin, end)           unchanged bytes in this chunk
//   corrected(texrect)          16-byte stack copy, omitted from guest
//   align(w0, w1)               original rectangle, before RT64 reads it
//   marker(payload)             E0574443 consumed; not submitted
//   fill(rect_w0, rect_w1, c0, c1)  menu-art coefficient, after its guest rect
//   held is not used here
// `hud_active` is the renderer's bracket flag. A marker callback may change
// it before the next command. The callback must clear it the way end-of-HUD
// does, including for an edge-fill marker, or later rectangles align against
// a bracket that already closed.
template <class Sink>
inline void walk_rdp(const uint8_t* memory, uint32_t start, uint32_t end,
                     int32_t logical_h, int32_t buffer_h, bool& hud_active,
                     RdpTask& task, Sink&& sink) {
    uint32_t run = start;
    auto guest_until = [&](uint32_t until) {
        if (run < until) sink.guest(run, until);
        run = until;
    };
    const bool tall = logical_h > 0 && int64_t(buffer_h) == int64_t(logical_h) * 2;
    for (uint32_t pos = start; pos < end; pos += 8) {
        const auto* words = reinterpret_cast<const uint32_t*>(memory + pos);
        const bool boundary = task.stream.remaining == 0;
        const unsigned op = (words[0] >> 24) & 63;
        if (task.hold && !boundary) {
            const Texrect fixed = correct_hres_texrect(
                {task.hold_w0, task.hold_w1, words[0], words[1]}, logical_h, buffer_h);
            sink.corrected(fixed);
            task.stream.consume(words[0]);
            task.hold = false;
            run = pos + 8;
            continue;
        }
        // Pause text is outside the HUD marker, so every ordinary texrect is
        // eligible. Menu edge fills are skipped here so their art coordinates
        // still drive the stretch fills.
        const bool texrect = op == 36 || op == 37;
        const bool whole = pos + 16 <= end;
        const bool split = pos + 8 == end;
        if (boundary && tall && !task.fill_active && texrect && (whole || split)) {
            Texrect in{words[0], words[1], 0, 0};
            Texrect fixed = in;
            if (whole) {
                const auto* coeff = reinterpret_cast<const uint32_t*>(memory + pos + 8);
                in.w2 = coeff[0];
                in.w3 = coeff[1];
                fixed = correct_hres_texrect(in, logical_h, buffer_h);
            }
            if (split || fixed != in) {
                task.stream.consume(words[0]);
                guest_until(pos);
                if (hud_active) sink.align(words[0], words[1]);
                if (split) {
                    task.hold_w0 = words[0];
                    task.hold_w1 = words[1];
                    task.hold = true;
                    run = pos + 8;
                } else {
                    sink.corrected(fixed);
                    task.stream.consume(reinterpret_cast<const uint32_t*>(memory + pos + 8)[0]);
                    pos += 8;
                    run = pos + 8;
                }
                continue;
            }
        }
        if (task.stream.consume(words[0])) {
            guest_until(pos);
            const uint32_t payload = words[1];
            sink.marker(payload);
            task.fill_active = payload == edge_fill;
            run = pos + 8;
        } else if (boundary && task.fill_active && op == 36) {
            task.fill_w0 = words[0];
            task.fill_w1 = words[1];
            task.fill_pending = true;
        } else if (!boundary && task.fill_pending && task.stream.remaining == 0) {
            guest_until(pos + 8);
            sink.fill(task.fill_w0, task.fill_w1, words[0], words[1]);
            task.fill_pending = false;
        } else if (boundary && hud_active && (op == 36 || op == 37)) {
            guest_until(pos);
            sink.align(words[0], words[1]);
        }
    }
    guest_until(end);
}
// The coefficient never arrived. The sink hands RT64 the original first word
// so its pending-command path matches an uncorrected stream.
template <class Sink>
inline void release_held_texrect(RdpTask& task, Sink&& sink) {
    if (!task.hold) return;
    sink.held(task.hold_w0, task.hold_w1);
    task.hold = false;
}
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
