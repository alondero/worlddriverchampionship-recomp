#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>

#define XXH_INLINE_ALL
#include "xxHash/xxh3.h"

// The hasher takes the tile decoder's field widths as part of its hash ABI.
constexpr uint8_t G_IM_SIZ_4b = 0, G_IM_SIZ_16b = 2, G_IM_SIZ_32b = 3, G_IM_FMT_RGBA = 0;
struct LoadTile {
    uint16_t line, tmem;
    uint8_t siz, fmt, palette;
};
#include "common/rt64_tmem_hasher.h"
namespace pinned {
// Select GCC's ctzll on MinGW, as portability patch 0005 does. The pinned
// header has no includes and tests __LP64__ only to select that intrinsic.
#if defined(__MINGW64__) && !defined(__LP64__)
#define __LP64__ 1
#define WDC_UNDEF_LP64
#endif
#include "pinned_tmem_hasher.h"
#ifdef WDC_UNDEF_LP64
#undef __LP64__
#undef WDC_UNDEF_LP64
#endif
}

int main() {
    std::minstd_rand random(1234567);
    std::array<uint8_t, 4096> memory{};
    unsigned comparisons = 0;
    auto matches = [&](LoadTile tile, uint16_t width, uint16_t height, uint32_t tlut, uint32_t version) {
        ++comparisons;
        auto expected = pinned::RT64::TMEMHasher::hash(memory.data(), tile, width, height, tlut, version);
        auto actual = RT64::TMEMHasher::hash(memory.data(), tile, width, height, tlut, version);
        if (expected == actual) return true;
        std::fprintf(stderr, "Palette hash mismatch at comparison %u\n", comparisons);
        return false;
    };
    for (unsigned i = 0; i < 20000; ++i) {
        for (auto& byte : memory) byte = uint8_t(random() % (i % 3 == 0 ? 4 : 256));
        LoadTile tile{uint16_t(random() % 17), uint16_t(random() % 512),
                      uint8_t(random() % 4), uint8_t(random() % 5), uint8_t(random() % 16)};
        uint16_t width = 1 + random() % 64, height = 1 + random() % 32;
        unsigned tlut = random() % 3, version = 1 + random() % 5;
        if (!matches(tile, width, height, tlut, version)) return 1;
        for (unsigned j = 2048; j < memory.size(); ++j) memory[j] = uint8_t(random());
        if (!matches(tile, width, height, tlut, version)) return 1;
    }
    // Every contiguous CI8 range, including bit 63 and all four word boundaries.
    for (unsigned first = 0; first < 256; ++first) {
        for (unsigned count = 1; first + count <= 256; ++count) {
            for (unsigned j = 0; j < 2048; ++j) memory[j] = uint8_t(first + j % count);
            if (!matches({8, 0, 1, 2, 0}, 64, 32, 2, 5)) return 1;
        }
    }
    std::printf("%u production hashes matched the dependency pin\n", comparisons);
}
