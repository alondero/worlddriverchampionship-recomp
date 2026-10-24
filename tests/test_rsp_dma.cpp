#include "librecomp/rsp.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>

// Exercise the production helpers included by generated BOSS code. Both
// memories use N64 byte order in word-swapped host storage; no ROM is needed.
uint8_t dmem[4096]{};

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main() {
    std::array<uint8_t, 8192> memory{};
    auto* rdram = memory.data();
    auto dram_byte = [&](uint32_t address) { return memory[(address & 0x1fffu) ^ 3u]; };
    // BOSS writes two 349-frame blocks as 512, 512, 372 byte transfers.
    // The second block starts four bytes into an eight-byte DMA word. Hardware
    // aligns that address down and rounds the final transfer up to 376 bytes.
    // Copying only 372 leaves the last stereo frame from the previous ring lap.
    memory.fill(0xa5);
    for (unsigned i = 0; i < 512; ++i) RSP_MEM_BU(i, 0xc20) = uint8_t(i);
    for (unsigned block = 0; block < 2; ++block) {
        for (unsigned offset : {0u, 512u, 1024u}) {
            const auto length = offset == 1024 ? 372u : 512u;
            dma_dmem_to_rdram(rdram, 0xc20, 0x80000100u + block * 1396 + offset, length - 1);
        }
    }
    for (unsigned i = 0; i < 4; ++i) {
        require(dram_byte(0x80000100u + 2792 - 4 + i) == uint8_t(372 + i),
            "BOSS DMA left stale stereo PCM at the synthesis block boundary");
    }
    require(dram_byte(0x80000100u + 2792) == 0xa5, "BOSS DMA exceeded rounded output");

    // Check both directions, including minimum transfers and a full DMEM
    // page. Guard bytes detect over-copying; high address bits and low three
    // DRAM address bits must retain the helpers' existing masking behavior.
    for (unsigned length : {1u, 4u, 7u, 8u, 9u, 372u, 512u, 4096u}) {
        const unsigned rounded = (length + 7) & ~7u;
        memory.fill(0xa5);
        for (unsigned i = 0; i < 4096; ++i) RSP_MEM_BU(i, 0) = uint8_t(i * 37 + 11);
        dma_dmem_to_rdram(rdram, 0, 0x80000107u, length - 1);
        for (unsigned i = 0; i < rounded; ++i)
            require(dram_byte(0x80000100u + i) == uint8_t(i * 37 + 11), "short RSP DMA write");
        require(dram_byte(0x800000ffu) == 0xa5 && dram_byte(0x80000100u + rounded) == 0xa5,
            "RSP DMA write crossed rounded bounds");

        for (unsigned i = 0; i < 4096; ++i) RSP_MEM_BU(i, 0) = 0xa5;
        dma_rdram_to_dmem(rdram, 0, 0x80000107u, length - 1);
        for (unsigned i = 0; i < rounded; ++i)
            require(RSP_MEM_BU(i, 0) == uint8_t(i * 37 + 11), "short RSP DMA read");
        if (rounded < 4096) require(RSP_MEM_BU(rounded, 0) == 0xa5, "RSP DMA read exceeded rounded bounds");
    }
    std::puts("RSP DMA transfer rounding passed");
}
