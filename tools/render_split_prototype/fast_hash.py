"""Experimental palette-index saturation and contiguous palette hash updates."""


def make_fast_hasher(original, cache_palette=False, batch_hash=False):
    source = original.replace("TMEMHasher", "PrototypeFastTMEMHasher").replace("bitScanForward64", "prototypeBitScanForward64")
    if "static constexpr auto masks" not in source:
        source = optimize_legacy_palette(source)
    if cache_palette:
        start = '                if ((version >= 5) && usesTLUT) {'
        end = '                }\n            };\n\n            auto hashTMEM'
        if source.count(start) != 1 or source.count(end) != 1:
            raise RuntimeError("Palette index cache seam changed")
        source = source.replace(start, '''                if ((version >= 5) && usesTLUT) {
                    // Cache only index membership, never palette values or a
                    // final texture hash. Exact bytes guard fingerprint collisions.
                    struct Entry {
                        uint64_t key = 0, bits[4] = {};
                        uint32_t size = 0;
                        bool ci4 = false;
                        uint8_t bytes[2048] = {};
                    };
                    static thread_local Entry entries[256];
                    Entry* entry = nullptr;
                    uint64_t key = 0;
                    const bool ci4 = loadTile.siz == G_IM_SIZ_4b;
                    if (byteCount >= 64 && byteCount <= 2048) {
                        ++paletteLookups;
                        key = XXH3_64bits(tmemBytes, byteCount);
                        entry = &entries[key & 255];
                        if (entry->size == byteCount && entry->ci4 == ci4 && entry->key == key &&
                                std::memcmp(entry->bytes, tmemBytes, byteCount) == 0) {
                            ++paletteHits;
                            for (unsigned b = 0; b < 4; ++b) tlutBitset[b] |= entry->bits[b];
                            return;
                        }
                    }
                    // Compute this chunk independently: accumulated membership
                    // from earlier rows must not enter the cache entry.
                    uint64_t prior[4];
                    for (unsigned b = 0; b < 4; ++b) { prior[b] = tlutBitset[b]; tlutBitset[b] = 0; }''')
        source = source.replace(end, '''                    if (entry) {
                        entry->key = key; entry->size = byteCount; entry->ci4 = ci4;
                        std::memcpy(entry->bytes, tmemBytes, byteCount);
                        for (unsigned b = 0; b < 4; ++b) entry->bits[b] = tlutBitset[b];
                    }
                    for (unsigned b = 0; b < 4; ++b) tlutBitset[b] |= prior[b];
                }
            };

            auto hashTMEM''')
        marker = '        static uint64_t hash('
        if source.count(marker) != 1:
            raise RuntimeError("Palette cache counters seam changed")
        source = '#include <cstring>\n' + source.replace(marker,
            '        static inline thread_local uint64_t paletteLookups = 0, paletteHits = 0;\n' + marker)
    if batch_hash:
        state = '            XXH3_state_t xxh3;\n            XXH3_64bits_reset(&xxh3);'
        digest = '            return XXH3_64bits_digest(&xxh3);'
        if source.count(state) != 1 or source.count(digest) != 1:
            raise RuntimeError("Buffered texture hash seam changed")
        source = source.replace(state, '            prototype::BufferedHash<> byteStream;')
        source = source.replace('XXH3_64bits_update(&xxh3, ', 'byteStream.append(')
        source = source.replace(digest, '            return byteStream.digest();')
        if '&xxh3' in source:
            raise RuntimeError("Unconverted buffered hash state use")
        source = '#include "buffered_hash.h"\n' + source
    return source


def optimize_legacy_palette(source):
    ci4 = "for (uint32_t i = 0; i < byteCount; i++) {\n                            tlutBitset[0]"
    if source.count(ci4) != 1:
        raise RuntimeError("CI4 palette scan seam changed")
    source = source.replace(ci4, "for (uint32_t i = 0; i < byteCount && tlutBitset[0] != UINT16_MAX; i++) {\n                            tlutBitset[0]")
    ci8 = "for (uint32_t i = 0; i < byteCount; i++) {\n                            tlutBitset[tmemBytes[i]"
    if source.count(ci8) != 1:
        raise RuntimeError("CI8 palette scan seam changed")
    source = source.replace(ci8, "for (uint32_t i = 0; i < byteCount; i++) {\n                            if ((i & 63) == 0 && (tlutBitset[0] & tlutBitset[1] & tlutBitset[2] & tlutBitset[3]) == UINT64_MAX) break;\n                            tlutBitset[tmemBytes[i]")
    palette = """                                    XXH3_64bits_update(&xxh3, &TMEM[paletteAddress + paletteIndex * 8], 8);
                                    tlutBitset[i] &= ~(1ULL << bitsetIndex);"""
    if source.count(palette) != 1:
        raise RuntimeError("Sparse palette hash seam changed")
    source = source.replace(palette, """                                    // A full 64-bit set took the existing fast path above.
                                    // Here the shifted set has 1..63 trailing ones;
                                    // grouping them preserves the exact byte stream.
                                    const uint64_t shifted = tlutBitset[i] >> bitsetIndex;
                                    const uint32_t runLength = prototypeBitScanForward64(~shifted);
                                    XXH3_64bits_update(&xxh3, &TMEM[paletteAddress + paletteIndex * 8], runLength * 8);
                                    tlutBitset[i] &= ~(((1ULL << runLength) - 1) << bitsetIndex);""")
    return source
