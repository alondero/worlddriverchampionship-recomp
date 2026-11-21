#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "librecomp/rsp.hpp"
#include "librecomp/rsp_vu_impl.hpp"

// Synthetic RSP DMEM: 4096 bytes, N64 byte order in word-swapped host storage.
// This fixture exercises the dependency helpers used by generated BOSS code.
uint8_t dmem[4096]{};

static void check(const RSP::r128& actual, const RSP::r128& expected, const char* op) {
    for (unsigned byte = 0; byte < 16; ++byte) {
        if (actual.byte(byte) != expected.byte(byte)) {
            std::fprintf(stderr, "%s byte %u: got %02x, expected %02x\n",
                op, byte, actual.byte(byte), expected.byte(byte));
            std::exit(1);
        }
    }
}

template<unsigned element>
static void exercise(RSP& rsp, uint32_t address) {
    RSP::r128 actual{}, expected{}, packed{};
    const uint32_t base = address & ~7u;
    const uint32_t index = (address & 7u) - element;
    auto byte = [&](unsigned offset) { return uint16_t(dmem[((base + ((index + offset) & 15u)) & 4095u) ^ 3u]); };

    for (unsigned lane = 0; lane < 8; ++lane) expected.element(lane) = byte(lane) * 128u;
    rsp.LUV<element>(actual, address, 0);
    check(actual, expected, "LUV");

    for (unsigned lane = 0; lane < 8; ++lane) expected.element(lane) = byte(lane * 2u) * 128u;
    rsp.LHV<element>(actual, address, 0);
    check(actual, expected, "LHV");

    for (unsigned lane = 0; lane < 8; ++lane) expected.element(lane) = byte(lane) * 256u;
    rsp.LPV<element>(actual, address, 0);
    check(actual, expected, "LPV");

    for (unsigned lane = 0; lane < 4; ++lane) {
        packed.element(lane) = byte(lane * 4u) * 128u;
        packed.element(lane + 4) = byte(lane * 4u + 8u) * 128u;
    }
    for (unsigned b = 0; b < 16; ++b) actual.byte(b) = expected.byte(b) = 0xa5;
    for (unsigned b = element; b < std::min(element + 8u, 16u); ++b) expected.byte(b) = packed.byte(b);
    rsp.LFV<element>(actual, address, 0);
    check(actual, expected, "LFV");
}

int main() {
    RSP rsp{};
    RSP::r128 lanes{};
    for (unsigned i = 0; i < 8; ++i) lanes.element(i) = 0x1234u + i * 0x101u;
    for (unsigned selector = 0; selector < 16; ++selector) {
        const unsigned group = selector < 2 ? 1 : selector < 4 ? 2 : selector < 8 ? 4 : 8;
        RSP::r128 expected{};
        for (unsigned lane = 0; lane < 8; ++lane)
            expected.element(lane) = lanes.element((lane & ~(group - 1)) | (selector & (group - 1)));
        check(lanes(selector), expected, "vector element selector");
    }
    for (unsigned address = 0xff0; address < 0x1010; ++address) {
        for (unsigned i = 0; i < sizeof(dmem); ++i) dmem[i] = uint8_t(i * 29);
        RSP::r128 loaded = lanes, expected = lanes;
        const unsigned count = 16 - (address & 15);
        for (unsigned i = 0; i < count; ++i) expected.byte(i) = dmem[((address + i) & 4095) ^ 3];
        rsp.LQV<0>(loaded, address, 0);
        check(loaded, expected, "LQV");
        auto memory = std::to_array(dmem);
        for (unsigned i = 0; i < count; ++i) memory[((address + i) & 4095) ^ 3] = lanes.byte(i);
        rsp.SQV<0>(lanes, address, 0);
        if (!std::equal(memory.begin(), memory.end(), dmem)) {
            std::fprintf(stderr, "SQV mismatch at %x\n", address);
            return 1;
        }
    }
    // The first case includes 0x80: LUV must yield 0x4000, never 0xc000.
    for (unsigned seed = 128; seed < 384; ++seed) {
        for (unsigned b = 0; b < 4096; ++b) dmem[b ^ 3u] = uint8_t(seed + b * 37u);
        for (uint32_t base : {0x100u, 0xff8u}) {
            for (unsigned alignment = 0; alignment < 8; ++alignment) {
                const auto address = base + alignment;
#define CASE(e) exercise<e>(rsp, address)
                CASE(0); CASE(1); CASE(2); CASE(3); CASE(4); CASE(5); CASE(6); CASE(7);
                CASE(8); CASE(9); CASE(10); CASE(11); CASE(12); CASE(13); CASE(14); CASE(15);
#undef CASE
            }
        }
    }
    std::puts("RSP packed unsigned-byte loads passed");
}
