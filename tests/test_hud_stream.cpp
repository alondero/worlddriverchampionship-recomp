#undef NDEBUG
#include "game_hud.h"
#include <cassert>
#include <cmath>

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
    const Texrect position_doubled{0xE40E01C0u, 0x000A0190u, 0x00000740u, 0x02000200u};
    assert(correct_hres_texrect(position_doubled, 240, 480) == native);
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
    // A flipped texrect uses opcode 37 and the same step words.
    const Texrect flipped{0xE50E01F0u, 0x000A0190u, 0x00000740u, 0x02000100u};
    const Texrect flipped_native{0xE50E00F8u, 0x000A00C8u, 0x00000740u, 0x02000200u};
    assert(correct_hres_texrect(flipped, 240, 480) == flipped_native);
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
}
