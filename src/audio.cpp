#include "audio.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
uint16_t device_period{};
SDL_AudioDeviceID device{}; // Accessed only by pump(), on the SDL main thread.
size_t limit() { return std::max<size_t>(requested_frequency / 4, 1); }
size_t reserve() {
    // Target three 30 Hz game updates or three SDL callback periods. SDL may
    // obtain a different period than the requested 512 frames.
    // Leave 50 ms below the 250 ms backlog ceiling so a 30 Hz producer batch
    // can cross the start threshold without discarding the paused queue.
    const auto maximum = std::max<size_t>(requested_frequency / 5, 1);
    return std::min(maximum, std::max<size_t>(requested_frequency / 10, device_period * 3));
}
bool statistics() {
    static const bool enabled = [] {
        const char* value = std::getenv("WDC_AUDIO_STATS");
        return value && value[0] && value[0] != '0';
    }();
    return enabled;
}
void rebuffer() {
    SDL_PauseAudioDevice(device, 1);
    prime = true;
}
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
    device_period = 0;
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
        else if (device) { rebuffer(); SDL_ClearQueuedAudio(device); device_frames = 0; }
        device_generation = generation;
        retry_at = 0;
    }
    if (requested_frequency && !device && now >= retry_at) {
        SDL_AudioSpec wanted{};
        wanted.freq = requested_frequency; wanted.format = AUDIO_S16SYS;
        wanted.channels = 2; wanted.samples = 512;
        SDL_AudioSpec obtained{};
        // Keep SDL's persistent internal conversion at the guest rate/format.
        // With no allowed format/rate changes, queued bytes stay s16 stereo
        // guest frames even when SDL converts to the physical device format.
        device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
        if (device) {
            device_frequency = requested_frequency;
            device_period = obtained.samples;
            if (statistics()) std::fprintf(stderr, "[wdc] audio opened: Hz=%u period=%u reserve=%zu frames\n",
                device_frequency, device_period, reserve());
        }
        else {
            retry_at = now + 1000;
            std::fprintf(stderr, "[wdc] audio unavailable; retrying: %s\n", SDL_GetError());
        }
    }
    device_frames = device ? SDL_GetQueuedAudioSize(device) / 4 : 0;
    if (device && !prime && !device_frames) {
        rebuffer();
        if (statistics()) std::fprintf(stderr, "[wdc] audio starved; rebuilding PCM reserve\n");
    }
    if (device && !pending.empty()) {
        // A bounded lead absorbs the game's 30 Hz updates. Recover from stalls
        // using recent sound, rather than retaining seconds of stale output.
        if (device_frames + pending_frames > limit()) {
            rebuffer();
            SDL_ClearQueuedAudio(device);
            device_frames = 0;
            if (statistics()) std::fprintf(stderr, "[wdc] audio backlog discarded\n");
        }
        bool failed = false;
        for (const auto& samples : pending) {
            if (failed) break;
            // queue_samples() trims pending output to at most 250 ms.
            const auto bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
            failed = SDL_QueueAudio(device, samples.data(), bytes) != 0;
        }
        if (failed) {
            std::fprintf(stderr, "[wdc] audio submission failed; reopening: %s\n", SDL_GetError());
            close_device();
            retry_at = now + 1000;
        } else if (prime && SDL_GetQueuedAudioSize(device) / 4 >= reserve()) {
            SDL_PauseAudioDevice(device, 0);
            prime = false;
            if (statistics()) std::fprintf(stderr, "[wdc] audio playback started\n");
        }
    }
    pending.clear(); pending_frames = 0;
    device_frames = device ? SDL_GetQueuedAudioSize(device) / 4 : 0;
}
