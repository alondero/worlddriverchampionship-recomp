#pragma once
#include <array>
#include <cstdint>

namespace wdc::audio {
// AI descriptors contain guest addresses, never host pointers. The calling
// guest thread supplies a synchronous sink when advancing the sample clock.
// Remaining length describes the current DMA only, not the next DMA or SDL.
class Dma {
public:
    using Sink = void (*)(void*, uint32_t address, uint32_t bytes);
    void reset(uint32_t frequency);
    bool enqueue(uint32_t address, uint32_t bytes);
    void advance(uint64_t nanoseconds, Sink sink, void* context);
    uint32_t status() const;
    uint32_t remaining() const;
private:
    struct Buffer { uint32_t address{}, bytes{}, offset{}; };
    std::array<Buffer, 2> buffers{};
    unsigned count{};
    uint32_t frequency = 48000; // Runtime's initial DAC rate, before the game sets it.
    uint64_t fraction{};
};
}
