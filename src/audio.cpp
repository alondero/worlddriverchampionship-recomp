#include "audio.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <deque>
#include <mutex>
#include <vector>

namespace {
std::mutex mutex;
std::deque<std::vector<int16_t>> pending;
uint32_t requested_frequency{}, device_frequency{};
size_t pending_frames{}, device_frames{};
uint64_t generation{}, device_generation{}, retry_at{};
bool prime = true;
SDL_AudioDeviceID device{}; // Accessed only by pump(), on the SDL main thread.
size_t limit() { return std::max<size_t>(requested_frequency / 4, 1); }
void trim(size_t maximum) {
    while (pending_frames > maximum && !pending.empty()) {
        auto& first = pending.front();
        const auto remove = std::min(pending_frames - maximum, first.size() / 2);
        if (remove == first.size() / 2) pending.pop_front();
        else first.erase(first.begin(), first.begin() + remove * 2);
        pending_frames -= remove;
    }
}
void close_device() {
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
    device_frequency = 0;
    device_frames = 0;
    prime = true;
}
}
void wdc::audio::set_frequency(uint32_t frequency) {
    std::lock_guard lock(mutex);
    requested_frequency = frequency;
    ++generation;
    pending.clear(); pending_frames = device_frames = 0;
}
void wdc::audio::queue_samples(int16_t* samples, size_t count) {
    count &= ~size_t(1);
    if (!count) return;
    std::vector<int16_t> stereo(count);
    for (size_t i = 0; i + 1 < count; i += 2) {
        stereo[i] = samples[i + 1]; stereo[i + 1] = samples[i];
    }
    std::lock_guard lock(mutex);
    pending_frames += count / 2;
    pending.push_back(std::move(stereo));
    trim(limit());
}
size_t wdc::audio::frames_remaining() {
    std::lock_guard lock(mutex);
    return pending_frames + device_frames;
}
void wdc::audio::pump() {
    std::lock_guard lock(mutex);
    const auto now = SDL_GetTicks64();
    if (device_generation != generation) {
        if (requested_frequency != device_frequency) close_device();
        else if (device) { SDL_ClearQueuedAudio(device); device_frames = 0; prime = true; }
        device_generation = generation;
        retry_at = 0;
    }
    if (requested_frequency && !device && now >= retry_at) {
        SDL_AudioSpec wanted{};
        wanted.freq = requested_frequency; wanted.format = AUDIO_S16SYS;
        wanted.channels = 2; wanted.samples = 512;
        device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
        if (device) device_frequency = requested_frequency;
        else {
            retry_at = now + 1000;
            std::fprintf(stderr, "[wdc] audio unavailable; retrying: %s\n", SDL_GetError());
        }
    }
    device_frames = device ? SDL_GetQueuedAudioSize(device) / 4 : 0;
    if (device && !pending.empty()) {
        // A bounded lead absorbs the game's 30 Hz updates. Recover from stalls
        // using recent sound, rather than retaining seconds of stale output.
        if (device_frames + pending_frames > limit()) {
            SDL_ClearQueuedAudio(device);
            device_frames = 0;
            prime = true;
        }
        bool failed = false;
        if (prime) {
            const size_t lead = requested_frequency / 16;
            trim(limit() - lead);
            const std::vector<int16_t> silence(lead * 2);
            failed = SDL_QueueAudio(device, silence.data(), silence.size() * sizeof(int16_t)) != 0;
        }
        for (const auto& samples : pending) {
            if (failed) break;
            failed = SDL_QueueAudio(device, samples.data(), samples.size() * sizeof(int16_t)) != 0;
        }
        if (failed) {
            std::fprintf(stderr, "[wdc] audio submission failed; reopening: %s\n", SDL_GetError());
            close_device();
            retry_at = now + 1000;
        } else {
            if (prime) SDL_PauseAudioDevice(device, 0);
            prime = false;
        }
    }
    pending.clear(); pending_frames = 0;
    device_frames = device ? SDL_GetQueuedAudioSize(device) / 4 : 0;
}
