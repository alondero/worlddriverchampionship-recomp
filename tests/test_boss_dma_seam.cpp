// Pins the compile-time seam between the generated BOSS unit and the port.
//
// src/aspMain.cpp is produced by RSPRecomp and is not tracked, so it cannot
// be edited to call the port. The build force-includes src/boss_dma.h into
// it instead, and that header replaces the runtime's DO_DMA_WRITE macro with
// the port's wrapper. This file is compiled the same way and repeats the
// generated unit's include order, so the seam fails here if the force-
// include, the header's override, or the runtime header's include guard
// changes underneath it.
#include "librecomp/rsp.hpp"
#include <cstdio>
#include <cstdlib>

// The runtime's DMEM, which the inline DMA helpers read and write.
uint8_t dmem[4096]{};

namespace {
struct DmaCall {
    bool seen = false;
    uint8_t* rdram = nullptr;
    uint32_t dmem_addr = 0, dram_addr = 0, length = 0;
} call;
}

// Stands in for the port's wrapper (src/boss_rsp.cpp), which is not linked
// here: this test records what the macro handed to it.
void wdc_boss_dma_write(uint8_t* rdram, uint32_t dmem_addr, uint32_t dram_addr, uint32_t wr_len) {
    call = {true, rdram, dmem_addr, dram_addr, wr_len};
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main() {
    // The generated unit's bossMain() keeps the DMA addresses in locals the
    // macro reads by name, so this test does the same.
    uint8_t rdram[0x1000]{};
    uint32_t dma_mem_address = 0x100;
    uint32_t dma_dram_address = 0x80000200u;

    DO_DMA_WRITE(7);

    require(call.seen, "DO_DMA_WRITE no longer routes through the port's DMA wrapper");
    require(call.rdram == rdram, "the port's DMA wrapper was given the wrong RDRAM pointer");
    require(call.dmem_addr == 0x100, "the port's DMA wrapper was given the wrong DMEM address");
    require(call.dram_addr == 0x80000200u, "the port's DMA wrapper was given the wrong DRAM address");
    require(call.length == 7, "the port's DMA wrapper was given the wrong length");

    // The runtime's helper must not have run: it would have written the
    // rounded eight-byte word into guest memory instead.
    for (uint8_t byte : rdram) require(byte == 0, "the runtime's DMA helper ran instead of the port's wrapper");

    // The range the port derives from the same arguments is what the capture
    // journal records; it must match the runtime's rounding, which
    // tests/test_rsp_dma.cpp checks against the bytes actually written.
    const auto range = wdc_boss_dma_write_range(call.dram_addr, call.length);
    require(range.address == 0x200 && range.bytes == 8, "the port's DMA range does not match the runtime's rounding");
    std::puts("BOSS DMA seam passed");
}
