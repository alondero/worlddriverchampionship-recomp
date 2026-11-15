"""Experimental palette-index saturation in a copied RT64 hasher."""


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
    return source
