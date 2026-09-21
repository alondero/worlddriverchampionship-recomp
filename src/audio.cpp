#include "audio.h"
#include <SDL.h>
#include <cstdio>
#include <deque>
#include <mutex>
#include <vector>

namespace {
std::mutex mutex;
std::deque<std::vector<int16_t>> pending;
uint32_t requested_frequency{}, device_frequency{};
size_t pending_frames{}, device_frames{};
SDL_AudioDeviceID device{}; // Accessed only by pump(), on the SDL main thread.
}
void wdc::audio::set_frequency(uint32_t frequency) {
    std::lock_guard lock(mutex);
    requested_frequency = frequency;
    pending.clear(); pending_frames = device_frames = 0;
}
void wdc::audio::queue_samples(int16_t* samples, size_t count) {
    std::vector<int16_t> stereo(count);
    for (size_t i = 0; i + 1 < count; i += 2) {
        stereo[i] = samples[i + 1]; stereo[i + 1] = samples[i];
    }
    std::lock_guard lock(mutex);
    pending_frames += count / 2;
    pending.push_back(std::move(stereo));
}
size_t wdc::audio::frames_remaining() {
    std::lock_guard lock(mutex);
    return pending_frames + device_frames;
}
void wdc::audio::pump() {
    std::lock_guard lock(mutex);
    if (requested_frequency && requested_frequency != device_frequency) {
        if (device) SDL_CloseAudioDevice(device);
        SDL_AudioSpec wanted{};
        wanted.freq = requested_frequency; wanted.format = AUDIO_S16SYS;
        wanted.channels = 2; wanted.samples = 1024;
        device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
        device_frequency = requested_frequency;
        if (device) SDL_PauseAudioDevice(device, 0);
        else std::fprintf(stderr, "[wdc] audio unavailable: %s\n", SDL_GetError());
    }
    for (auto& samples : pending) {
        if (device) SDL_QueueAudio(device, samples.data(), samples.size() * sizeof(int16_t));
    }
    pending.clear(); pending_frames = 0;
    device_frames = device ? SDL_GetQueuedAudioSize(device) / 4 : 0;
}
