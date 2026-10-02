#include "audio_dma.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Memory {
    std::vector<uint8_t> ring = std::vector<uint8_t>(0x4000);
    std::vector<uint8_t> played;
    uint64_t bytes{};
    static void copy(void* context, uint32_t address, uint32_t length) {
        auto& memory = *static_cast<Memory*>(context);
        const uint32_t offset = address - 0x803F0400;
        require(offset <= memory.ring.size() && length <= memory.ring.size() - offset, "DMA read outside ring");
        memory.played.insert(memory.played.end(), memory.ring.begin() + offset, memory.ring.begin() + offset + length);
        memory.bytes += length;
    }
};
}
int main() {
    try {
        wdc::audio::Dma dma;
        Memory memory;
        // WDC's initializer enqueues two silent rings BEFORE setting 22000 Hz.
        require(dma.enqueue(0x803F0400, 0x4000) && dma.enqueue(0x803F0400, 0x4000),
            "WDC startup buffers were rejected before frequency selection");
        dma.reset(22000);
        require(dma.enqueue(0x803F0400, 0x4000), "first DMA rejected");
        require(dma.status() == 0x40000000 && dma.remaining() == 0x4000, "current DMA not busy");
        require(dma.enqueue(0x803F0400, 0x4000), "second DMA rejected");
        require(dma.status() == 0xC0000000 && !dma.enqueue(0x803F0400, 0x4000), "FIFO did not fill");
        require(dma.remaining() == 0x4000, "length included the next DMA");
        // The ring is filled AFTER enqueue: eager copying plays old samples.
        std::fill(memory.ring.begin(), memory.ring.end(), 0x5A);
        dma.advance(10000000, Memory::copy, &memory);
        require(memory.bytes == 880 && dma.remaining() == 0x4000 - 880, "DAC clock or remaining length wrong");
        require(memory.played.front() == 0x5A && memory.played.back() == 0x5A, "DMA copied the ring too early");
        dma.reset(22000);
        memory.bytes = 0;
        memory.played.clear();
        // Actual WDC pattern: status / set full ring if FIFO has room / length,
        // once per 30 Hz graphics frame. Output must advance at 22000 Hz.
        unsigned submissions = 0;
        for (unsigned frame = 0; frame < 150; ++frame) {
            dma.advance(33333333, Memory::copy, &memory);
            if (!(dma.status() & 0x80000000)) {
                require(dma.enqueue(0x803F0400, 0x4000), "WDC ring submission failed");
                ++submissions;
            }
            require(dma.remaining() <= 0x4000, "WDC ring cursor escaped");
        }
        require(submissions < 30 && memory.bytes >= 436000 && memory.bytes <= 440000,
            "30 Hz submissions produced an audio backlog");
        dma.advance(1000000000000ull, Memory::copy, &memory);
        require(dma.status() == 0 && dma.remaining() == 0, "stalled DMA did not drain");
        const auto before = memory.bytes;
        dma.advance(1000000000000ull, Memory::copy, &memory);
        require(dma.enqueue(0x803F0400, 8), "restart after idle failed");
        dma.advance(0, Memory::copy, &memory);
        require(memory.bytes == before && dma.remaining() == 8, "idle time consumed a fresh DMA");
        dma.reset(48000);
        require(!dma.status() && !dma.remaining(), "frequency reset retained DMA state");
        require(!dma.enqueue(0x803F0400, 4) && !dma.enqueue(0x803F0400, 0x40000), "invalid DMA sizes accepted");
        std::puts("audio DMA tests passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
