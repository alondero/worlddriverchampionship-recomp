// Force-included into the generated src/aspMain.cpp (see CMakeLists.txt) so
// BOSS's RDRAM writes pass through wdc_boss_dma_write in boss_rsp.cpp, which
// journals them while a WDC_BOSS_CAPTURE recording is active. rsp.hpp's
// include guard makes the generated file's own include a no-op, so this
// definition wins at every call site. Reads keep the runtime helper.
#pragma once
#include "librecomp/rsp.hpp"

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
