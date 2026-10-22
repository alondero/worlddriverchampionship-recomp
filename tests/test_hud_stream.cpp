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
}
