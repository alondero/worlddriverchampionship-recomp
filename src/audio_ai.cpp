#include "audio_dma.h"
#include "guest_memory.h"
#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include <chrono>
#include <mutex>

namespace {
std::mutex mutex;
wdc::audio::Dma dma;
auto last = std::chrono::steady_clock::now();
void submit(void* context, uint32_t address, uint32_t bytes) {
    ultramodern::queue_audio_buffer(static_cast<uint8_t*>(context), int32_t(address), bytes);
}
void advance(uint8_t* rdram) {
    const auto now = std::chrono::steady_clock::now();
    dma.advance(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count(), submit, rdram);
    last = now;
}
}
// WDC polls SDK AI entries on the game thread before each BOSS audio update.
// Its ring at 0x803F0400 contains 0x4000 bytes of word-swapped s16 stereo at
// 22000 frames/s. Emulate the two DMA slots and current 8-byte length register;
// copy only the elapsed range before the next ring writes, using this call's
// runtime-owned RDRAM. No guest pointer survives a call and no SDL runs here.
// All descriptors must be 8-byte aligned cached KSEG0 ranges in 8 MiB RDRAM.
// Invalid addresses abort before access; invalid sizes/rates or a full FIFO
// return SDK failure (-1). Runtime VI already dispatches WDC's AI event.
// Remove these hooks when upstream supplies equivalent streaming AI semantics.
extern "C" void wdc_ai_set_frequency(uint8_t*, recomp_context* ctx) {
    const uint32_t frequency = ctx->r4;
    if (!frequency || frequency > 192000) { ctx->r2 = -1; return; }
    std::lock_guard lock(mutex);
    dma.reset(frequency);
    last = std::chrono::steady_clock::now();
    ultramodern::set_audio_frequency(frequency);
    ctx->r2 = frequency;
}
extern "C" void wdc_ai_set_buffer(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t address = ctx->r4, bytes = ctx->r5;
    if (!bytes || (bytes & 7) || bytes > 0x3FFF8) { ctx->r2 = -1; return; }
    wdc_require_guest_range(address, bytes, 8);
    std::lock_guard lock(mutex);
    advance(rdram);
    ctx->r2 = dma.enqueue(address, bytes) ? 0 : -1;
}
extern "C" void wdc_ai_get_status(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    advance(rdram);
    ctx->r2 = int32_t(dma.status());
}
extern "C" void wdc_ai_get_length(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    advance(rdram);
    ctx->r2 = dma.remaining();
}
