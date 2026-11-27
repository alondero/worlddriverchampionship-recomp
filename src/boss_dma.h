// Force-included into the generated src/aspMain.cpp (see CMakeLists.txt) so
// BOSS's RDRAM writes pass through wdc_boss_dma_write in boss_rsp.cpp, which
// journals them while a WDC_BOSS_CAPTURE recording is active. rsp.hpp's
// include guard makes the generated file's own include a no-op, so this
// definition wins at every call site. The same seam bounds BOSS fog reads.
#pragma once
#include "librecomp/rsp.hpp"
#include <algorithm>

// Physical RDRAM bytes dma_dmem_to_rdram writes for a DO_DMA_WRITE: the DRAM
// address masked to 24 bits and 8-byte aligned down, and the inclusive length
// rounded up to whole 8-byte words (patch 0018). wdc_rsp_dma pins this to the
// patched helper, so a rounding change there fails a test.
struct WdcDmaRange { uint32_t address, bytes; };
inline WdcDmaRange wdc_boss_dma_write_range(uint32_t dram_addr, uint32_t wr_len) {
    return {dram_addr & 0xFFFFF8, (wr_len | 7u) + 1};
}

void wdc_boss_dma_write(uint8_t* rdram, uint32_t dmem_addr, uint32_t dram_addr, uint32_t wr_len);
#undef DO_DMA_WRITE
#define DO_DMA_WRITE(wr_len) wdc_boss_dma_write(rdram, dma_mem_address, dma_dram_address, (wr_len))

// USA BOSS IMEM 1390/1394 compute the signed fog-table index in r8/r9
// and load one byte at 07B0 + index. The 256 entries occupy RSP DMEM
// 0730..082F, immediately before the graphics scratch area at 0830.
// The RSP worker owns DMEM. Preserve its word-swapped byte addressing and
// sign extension. Out-of-range fog uses the nearest table endpoint.
// All other signed byte accesses retain ordinary RSP wrap and store behavior.
template <int32_t Offset>
inline int8_t& wdc_boss_dmem_byte(uint32_t address) {
    if constexpr (Offset == 0x7B0) {
        address = uint32_t(std::clamp(int32_t(address), -128, 127));
    }
    return reinterpret_cast<int8_t&>(RSP_MEM_BU(Offset, address));
}
#undef RSP_MEM_B
#define RSP_MEM_B(offset, address) wdc_boss_dmem_byte<(offset)>(address)
