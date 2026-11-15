"""Experimental palette-index saturation and contiguous palette hash updates."""


def make_fast_hasher(original):
    source = original.replace("TMEMHasher", "PrototypeFastTMEMHasher").replace("bitScanForward64", "prototypeBitScanForward64")
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
