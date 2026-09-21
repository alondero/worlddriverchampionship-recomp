#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Native replacements access runtime-owned, word-swapped 8 MiB guest RDRAM
// synchronously on their calling guest thread. Never retain a guest pointer.
// These replacements accept cached KSEG0 addresses only; invalid ranges or
// alignment fail before access. Remove with equivalent generated-code support.
inline void wdc_require_guest_range(uint64_t address, uint32_t bytes, uint32_t alignment = 1) {
    const uint32_t addr = static_cast<uint32_t>(address);
    if (addr < 0x80000000u || bytes > 0x800000u ||
        addr - 0x80000000u > 0x800000u - bytes || (addr & (alignment - 1))) {
        std::fprintf(stderr, "[wdc] invalid native guest range: %08x + %x\n", addr, bytes);
        std::abort();
    }
}
