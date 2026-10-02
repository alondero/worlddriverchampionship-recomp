#include "audio_dma.h"
#include <algorithm>

void wdc::audio::Dma::reset(uint32_t rate) {
    frequency = rate;
    count = 0;
    fraction = 0;
}
bool wdc::audio::Dma::enqueue(uint32_t address, uint32_t bytes) {
    if (!frequency || count == buffers.size() || !bytes || (bytes & 7) || bytes > 0x3FFF8)
        return false;
    if (!count) fraction = 0; // Time spent idle cannot consume a new DMA.
    buffers[count++] = {address, bytes, 0};
    return true;
}
void wdc::audio::Dma::advance(uint64_t nanoseconds, Sink sink, void* context) {
    if (!count || !frequency) return;
    uint64_t queued_bytes = remaining();
    if (count == 2) queued_bytes += buffers[1].bytes;
    // Clamp long stalls to the queued duration, preventing overflow and catchup
    // output beyond the two hardware descriptors. One AI word is two frames.
    nanoseconds = std::min(nanoseconds, queued_bytes * 1000000000 / (frequency * 4ull) + 1000000000);
    const uint64_t ticks = nanoseconds * frequency + fraction;
    uint64_t bytes = (ticks / 2000000000) * 8;
    fraction = ticks % 2000000000;
    while (count && bytes) {
        auto& buffer = buffers[0];
        const auto consumed = uint32_t(std::min<uint64_t>(bytes, remaining()));
        sink(context, buffer.address + buffer.offset, consumed);
        buffer.offset += consumed;
        bytes -= consumed;
        if (buffer.offset == buffer.bytes) {
            buffers[0] = buffers[1];
            --count;
        }
    }
    if (!count) fraction = 0;
}
uint32_t wdc::audio::Dma::status() const {
    return (count ? 0x40000000u : 0) | (count == 2 ? 0x80000000u : 0);
}
uint32_t wdc::audio::Dma::remaining() const {
    return count ? buffers[0].bytes - buffers[0].offset : 0;
}
