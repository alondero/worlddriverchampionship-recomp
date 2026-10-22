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
}
