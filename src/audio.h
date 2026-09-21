#pragma once
#include <cstddef>
#include <cstdint>
namespace wdc::audio {
// Guest audio threads submit copied, word-swapped stereo buffers. The main
// SDL thread alone owns the device and drains this queue in pump(). No guest
// pointer survives submission; changing frequency discards pending old audio.
void set_frequency(uint32_t frequency);
void queue_samples(int16_t* samples, size_t count);
size_t frames_remaining();
void pump();
}
