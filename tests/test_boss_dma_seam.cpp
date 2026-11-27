// Pins the compile-time seams between the generated BOSS unit and the port.
//
// src/aspMain.cpp is produced by RSPRecomp and is not tracked, so it cannot
// be edited to call the port. The build force-includes src/boss_dma.h into
// it instead, and that header wraps the runtime's DMA writes and fog loads.
// This file is compiled the same way and repeats the
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

    // Simulate the actual signed BOSS loads and put non-fog bytes just past
    // the table. Index 231 read graphics scratch in the Rome A replay.
    constexpr uint32_t fog_base = 0x7B0;
    RSP_MEM_BU(fog_base, -128) = 0x03;
    RSP_MEM_BU(fog_base, -1) = 0x28;
    RSP_MEM_BU(fog_base, 127) = 0xF8;
    RSP_MEM_BU(fog_base, -129) = 0x45;
    RSP_MEM_BU(fog_base, 128) = 0x90;
    RSP_MEM_BU(fog_base, 231) = 0xAF;
    require(RSP_MEM_B(fog_base, -129) == 3, "BOSS fog read before the table");
    require(RSP_MEM_B(fog_base, -128) == 3, "BOSS fog lower endpoint changed");
    require(RSP_MEM_B(fog_base, -1) == 40, "in-range BOSS fog changed");
    require(RSP_MEM_B(fog_base, 127) == -8, "BOSS fog upper endpoint lost sign extension");
    require(RSP_MEM_B(fog_base, 128) == -8, "BOSS fog read the first byte after the table");
    require(RSP_MEM_B(fog_base, 231) == -8, "distant BOSS fog read graphics scratch");
    RSP_MEM_B(0xD, 0x200) = -9;
    require(RSP_MEM_B(0xD, 0x200) == -9, "ordinary BOSS signed byte accesses changed");
    RSP_MEM_B(-1, 0) = 7;
    require(RSP_MEM_BU(0, 0xFFF) == 7, "ordinary BOSS DMEM wrap changed");
    std::puts("BOSS DMA and fog seams passed");
}
