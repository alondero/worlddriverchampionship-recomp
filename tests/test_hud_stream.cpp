#undef NDEBUG
#include "game_hud.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Sample span RT64's drawRect builds before the >> 7. `vertical` selects the
// axis applied to screen Y: dtdy for an ordinary rect, dsdx when flipped.
// Width and height match FixedRect::width/height for 10.2 coordinates.
int64_t rdp_axis_span(wdc::hud::Texrect rect, bool vertical) {
    const bool flip = ((rect.w0 >> 24) & 63) == 37;
    const int32_t ulx = (rect.w1 >> 12) & 0xFFF, uly = rect.w1 & 0xFFF;
    const int32_t lrx = (rect.w0 >> 12) & 0xFFF, lry = rect.w0 & 0xFFF;
    const int32_t rect_w = ((lrx + 3) >> 2) - ((ulx + 3) >> 2);
    const int32_t rect_h = ((lry + 3) >> 2) - ((uly + 3) >> 2);
    const int32_t dsdx = int16_t(rect.w3 >> 16);
    const int32_t dtdy = int16_t(uint16_t(rect.w3));
    const int32_t along_x = (flip ? rect_h : rect_w) << 2;
    const int32_t along_y = (flip ? rect_w : rect_h) << 2;
    const int64_t s = int64_t(dsdx) * along_x;
    const int64_t t = int64_t(dtdy) * along_y;
    if (flip) return vertical ? s : t;
    return vertical ? t : s;
}

struct Submission {
    std::vector<std::vector<uint32_t>> guest_spans;
    std::vector<std::vector<uint32_t>> copies;
    std::vector<std::vector<uint32_t>> aligns;
    std::vector<std::vector<uint32_t>> fills;
    std::vector<uint32_t> held_words;
    const uint8_t* memory = nullptr;
    bool* hud = nullptr;
    void guest(uint32_t begin, uint32_t end) {
        std::vector<uint32_t> words((end - begin) / 4);
        std::memcpy(words.data(), memory + begin, end - begin);
        guest_spans.push_back(words);
    }
    void corrected(wdc::hud::Texrect fixed) {
        copies.push_back({fixed.w0, fixed.w1, fixed.w2, fixed.w3});
    }
    void align(uint32_t w0, uint32_t w1) { aligns.push_back({w0, w1}); }
    void marker(uint32_t) { if (hud) *hud = false; }
    void fill(uint32_t w0, uint32_t w1, uint32_t c0, uint32_t c1) {
        fills.push_back({w0, w1, c0, c1});
    }
    void held(uint32_t w0, uint32_t w1) { held_words = {w0, w1}; }
};

int main() {
    using namespace wdc::hud;
    // Every RDP triangle variant, including a marker-shaped coefficient split
    // across DMA chunks, must preserve framing. Texrect continuation too.
    for (unsigned opcode : {8, 9, 10, 11, 12, 13, 14, 15, 36, 37}) {
        Stream stream;
        assert(!stream.consume(opcode << 24));
        const unsigned words = command_words(opcode << 24);
        for (unsigned i = 1; i < words; ++i) assert(!stream.consume(marker));
        assert(stream.consume(marker));
        assert(stream.remaining == 0);
    }
    assert(edge_shift(320, 320, 1, 2) == 0);
    assert(edge_shift(320, 320, 2, 0) == 0);
    assert(std::abs(edge_shift(320, 320, 2, 1) - 160.0f / 3) < 0.001f);
    assert(edge_shift(320, 320, 2, 2) == 160);
    assert(edge_shift(0, 320, 2, 2) == -160);
    assert(edge_shift(160, 320, 2, 2) == 0); // internal split edge stays centered

    // Main-menu band art: 2x-magnified 152-pixel columns at x 8..160 and
    // 160..312 in a 320-pixel frame, 16:9 output (scale 4/3).
    const Fills left = edge_fills(8 * 4, 160 * 4, 0, 512, 320, 4.0f / 3);
    assert(left.count == 1);
    assert(left.fill[0].x0 == 0);
    assert(left.fill[0].x1 == 184 + 4); // shown art edge plus one pixel
    assert(left.fill[0].s == 8);        // first pixel centre: 0.25 texel
    const Fills right = edge_fills(160 * 4, 312 * 4, 0, 512, 320, 4.0f / 3);
    assert(right.count == 1);
    assert(right.fill[0].x0 == 1096 - 4 && right.fill[0].x1 == 320 * 4);
    assert(right.fill[0].s == 2424);    // last pixel centre: 75.75 texels
    // A full-width separator bar reaches both output edges.
    const Fills bar = edge_fills(8 * 4, 312 * 4, 64, 512, 320, 2.0f);
    assert(bar.count == 2 && bar.fill[0].x1 == 640 - 304 + 4 && bar.fill[1].x0 == 640 + 304 - 4);
    assert(bar.fill[0].s == 72);
    // Art touching only the 16-pixel overscan margin still counts; interior art does not.
    assert(edge_fills(16 * 4, 100 * 4, 0, 1024, 320, 2.0f).count == 1);
    assert(edge_fills(17 * 4, 303 * 4, 0, 1024, 320, 2.0f).count == 0);
    // 4:3 and narrow outputs have no space to fill; degenerate rects are ignored.
    assert(edge_fills(8 * 4, 312 * 4, 0, 512, 320, 1.0f).count == 0);
    assert(edge_fills(8 * 4, 312 * 4, 0, 512, 320, 0.75f).count == 0);
    assert(edge_fills(8 * 4, 8 * 4, 0, 512, 320, 2.0f).count == 0);
    assert(edge_fills(8 * 4, 312 * 4, 0, 512, 0, 2.0f).count == 0);

    // func_8006CCD4, sprite x=40 y=50 w=16 h=12. These words were captured
    // from the translated blitter plus the BOSS E4/E1/F1 rewrite, with the
    // H-RES flag clear and with it set. Mode 6's buffer is 480 lines and its
    // sampled picture is 240, so the doubled packet must become the clear one.
    const Texrect native{0xE40E00F8u, 0x000A00C8u, 0x00000740u, 0x02000200u};
    const Texrect height_doubled{0xE40E01F0u, 0x000A0190u, 0x00000740u, 0x02000100u};
    assert(correct_hres_texrect(height_doubled, 240, 480) == native);
    // Same sprite with byte +0x1C set: only the top moved, and dtdy stayed
    // 0x200. Halving both edges would squash the 12-pixel height.
    // The same step word is what the flag-clear blitter stores for an
    // unshifted rectangle. The coefficient does not prove that Y moved, so
    // this packet stays where the blitter put it.
    const Texrect position_doubled{0xE40E01C0u, 0x000A0190u, 0x00000740u, 0x02000200u};
    assert(correct_hres_texrect(position_doubled, 240, 480) == position_doubled);
    // func_8002DAD0's step word was not captured from a live packet. With
    // ra = 9, only-Y doubled stores 0x0200FF00 and both-axes doubled stores
    // 0x0100FF00. The rectangle here is the captured 2x sprite, so halving
    // both edges has a known result. Mode 8 keeps the 0x0100 X step.
    const Texrect digit_doubled{0xE40E01F0u, 0x000A0190u, 0x00000740u, 0x0200FF00u};
    const Texrect digit_native{0xE40E00F8u, 0x000A00C8u, 0x00000740u, 0x0200FE00u};
    assert(correct_hres_texrect(digit_doubled, 240, 480) == digit_native);
    const Texrect digit_wide{0xE40E01F0u, 0x000A0190u, 0x00000740u, 0x0100FF00u};
    const Texrect digit_wide_native{0xE40E00F8u, 0x000A00C8u, 0x00000740u, 0x0100FE00u};
    assert(correct_hres_texrect(digit_wide, 240, 480) == digit_wide_native);
    // RT64's drawRect swaps the sample axes when the rectangle is flipped:
    // dsdx (high half) covers screen Y and dtdy (low half) covers screen X.
    // Restoring the low half would change horizontal coverage. The only flip
    // emitter, func_8006CA7C, was not executed, so opcode 37 is left alone.
    const Texrect flipped{0xE50E01F0u, 0x000A0190u, 0x00000740u, 0x02000100u};
    assert(correct_hres_texrect(flipped, 240, 480) == flipped);
    assert(rdp_axis_span(correct_hres_texrect(flipped, 240, 480), false) == rdp_axis_span(flipped, false));
    assert(rdp_axis_span(correct_hres_texrect(flipped, 240, 480), true) == rdp_axis_span(flipped, true));
    assert(rdp_axis_span(correct_hres_texrect(height_doubled, 240, 480), true) == rdp_axis_span(native, true));
    assert(rdp_axis_span(correct_hres_texrect(height_doubled, 240, 480), false) == rdp_axis_span(native, false));
    // Menus (logical 480, buffer 480) and normal racing (240, 240) already
    // match the sampled picture. The background divider's 0x04000400 step,
    // a non-rect opcode, and an inverted position-only rect stay put.
    for (const Texrect& packet : {height_doubled, position_doubled, digit_doubled}) {
        assert(correct_hres_texrect(packet, 480, 480) == packet);
        assert(correct_hres_texrect(packet, 240, 240) == packet);
    }
    const Texrect divider{0xE40E01F0u, 0x000A0190u, 0x00000740u, 0x04000400u};
    assert(correct_hres_texrect(divider, 240, 480) == divider);
    const Texrect triangle{0xC80001F0u, 0x000A0190u, 0x00000740u, 0x02000100u};
    assert(correct_hres_texrect(triangle, 240, 480) == triangle);
    const Texrect inverted{0xE40E0010u, 0x000A0190u, 0x00000740u, 0x02000200u};
    assert(correct_hres_texrect(inverted, 240, 480) == inverted);
    assert(correct_hres_texrect(height_doubled, 0, 0) == height_doubled);
    assert(correct_hres_texrect(height_doubled, -240, -480) == height_doubled);
    assert(correct_hres_texrect(height_doubled, 240, 479) == height_doubled);

    // The graphics worker submits these packets. Pause text is the captured
    // height-doubled sprite outside the HUD marker. Digits use the recompiled
    // 0xFF00 step. The flag-clear sprite, the position-only sprite, the
    // divider and the flipped rect must reach RT64 unchanged.
    std::vector<uint32_t> chunk;
    auto push = [&](Texrect t) {
        chunk.insert(chunk.end(), {t.w0, t.w1, t.w2, t.w3});
    };
    push(height_doubled);
    push(digit_doubled);
    push(position_doubled);
    push(native);
    push(divider);
    chunk.insert(chunk.end(), {0xE7000000u, 0u});
    push(flipped);
    const std::vector<uint32_t> original = chunk;
    Submission out;
    out.memory = reinterpret_cast<const uint8_t*>(chunk.data());
    RdpTask task;
    bool hud_active = false;
    walk_rdp(reinterpret_cast<const uint8_t*>(chunk.data()), 0,
             uint32_t(chunk.size() * 4), 240, 480, hud_active, task, out);
    assert(chunk == original);
    assert(out.copies.size() == 2);
    assert(out.copies[0] == std::vector<uint32_t>({native.w0, native.w1, native.w2, native.w3}));
    assert(out.copies[1] == std::vector<uint32_t>({digit_native.w0, digit_native.w1, digit_native.w2, digit_native.w3}));
    assert(out.guest_spans.size() == 1);
    const std::vector<uint32_t> unchanged(original.begin() + 8, original.end());
    assert(out.guest_spans[0] == unchanged);
    assert(out.aligns.empty());
    assert(out.fills.empty());
    assert(!task.hold);

    // The coefficient arrives in the next BOSS chunk. The first word is held,
    // then one corrected rectangle is submitted. A trailing sync stays guest.
    {
        alignas(8) uint32_t first[2] = {height_doubled.w0, height_doubled.w1};
        alignas(8) uint32_t second[4] = {height_doubled.w2, height_doubled.w3, 0xE7000000u, 0u};
        Submission split;
        RdpTask split_task;
        bool split_hud = false;
        split.memory = reinterpret_cast<const uint8_t*>(first);
        walk_rdp(reinterpret_cast<const uint8_t*>(first), 0, 8, 240, 480, split_hud, split_task, split);
        split.memory = reinterpret_cast<const uint8_t*>(second);
        assert(split.copies.empty());
        assert(split.guest_spans.empty());
        assert(split_task.hold);
        assert(split_task.hold_w0 == height_doubled.w0 && split_task.hold_w1 == height_doubled.w1);
        walk_rdp(reinterpret_cast<const uint8_t*>(second), 0, 16, 240, 480, split_hud, split_task, split);
        assert(!split_task.hold);
        assert(split.copies.size() == 1);
        assert(split.copies[0] == std::vector<uint32_t>({native.w0, native.w1, native.w2, native.w3}));
        assert(split.guest_spans.size() == 1);
        assert(split.guest_spans[0] == std::vector<uint32_t>({0xE7000000u, 0u}));
    }
    // A task that ends on the held word gives RT64 the original 8 bytes.
    {
        alignas(8) uint32_t first[2] = {height_doubled.w0, height_doubled.w1};
        Submission tail;
        RdpTask tail_task;
        bool tail_hud = false;
        walk_rdp(reinterpret_cast<const uint8_t*>(first), 0, 8, 240, 480, tail_hud, tail_task, tail);
        release_held_texrect(tail_task, tail);
        assert(!tail_task.hold);
        assert(tail.copies.empty());
        assert(tail.held_words == std::vector<uint32_t>({height_doubled.w0, height_doubled.w1}));
    }
    // Menu edge-fill brackets keep the original art rectangle.
    {
        std::vector<uint32_t> menu;
        menu.insert(menu.end(), {marker, edge_fill});
        menu.insert(menu.end(), {height_doubled.w0, height_doubled.w1, height_doubled.w2, height_doubled.w3});
        Submission fills;
        fills.memory = reinterpret_cast<const uint8_t*>(menu.data());
        RdpTask fill_task;
        bool fill_hud = true;
        fills.hud = &fill_hud;
        walk_rdp(reinterpret_cast<const uint8_t*>(menu.data()), 0,
                 uint32_t(menu.size() * 4), 240, 480, fill_hud, fill_task, fills);
        assert(!fill_hud);
        assert(fill_task.fill_active);
        assert(fills.copies.empty());
        assert(fills.guest_spans.size() == 1);
        assert(fills.guest_spans[0] == std::vector<uint32_t>({
            height_doubled.w0, height_doubled.w1, height_doubled.w2, height_doubled.w3}));
        assert(fills.fills.size() == 1);
        assert(fills.fills[0] == std::vector<uint32_t>({
            height_doubled.w0, height_doubled.w1, height_doubled.w2, height_doubled.w3}));
    }
    // A 240-line picture does not rewrite the doubled packet.
    {
        alignas(8) uint32_t words[4] = {height_doubled.w0, height_doubled.w1, height_doubled.w2, height_doubled.w3};
        Submission flat;
        flat.memory = reinterpret_cast<const uint8_t*>(words);
        RdpTask flat_task;
        bool flat_hud = false;
        walk_rdp(reinterpret_cast<const uint8_t*>(words), 0, 16, 240, 240, flat_hud, flat_task, flat);
        assert(flat.copies.empty());
        assert(flat.guest_spans.size() == 1);
        assert(flat.guest_spans[0] == std::vector<uint32_t>({words[0], words[1], words[2], words[3]}));
    }
    // Inside the HUD bracket the original X is what alignment sees.
    {
        alignas(8) uint32_t words[8] = {
            height_doubled.w0, height_doubled.w1, height_doubled.w2, height_doubled.w3,
            position_doubled.w0, position_doubled.w1, position_doubled.w2, position_doubled.w3};
        Submission hud;
        hud.memory = reinterpret_cast<const uint8_t*>(words);
        RdpTask hud_task;
        bool inside = true;
        walk_rdp(reinterpret_cast<const uint8_t*>(words), 0, 32, 240, 480, inside, hud_task, hud);
        assert(hud.aligns.size() == 2);
        assert(hud.aligns[0] == std::vector<uint32_t>({height_doubled.w0, height_doubled.w1}));
        assert(hud.aligns[1] == std::vector<uint32_t>({position_doubled.w0, position_doubled.w1}));
        assert(hud.copies.size() == 1);
        assert(hud.copies[0][0] == native.w0 && hud.copies[0][1] == native.w1);
        assert(hud.guest_spans.size() == 1);
        assert(hud.guest_spans[0] == std::vector<uint32_t>({
            position_doubled.w0, position_doubled.w1, position_doubled.w2, position_doubled.w3}));
    }
}
