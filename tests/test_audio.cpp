#include "audio.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
uint64_t ticks;
unsigned opens;
bool fail_open = true, fail_queue;
std::vector<int16_t> played;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec* spec, SDL_AudioSpec*, int) {
    ++opens;
    require(spec->format == AUDIO_S16SYS && spec->channels == 2, "device format changed");
    return fail_open ? 0 : 1;
}
void SDL_CloseAudioDevice(SDL_AudioDeviceID) { played.clear(); }
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int) {}
void SDL_ClearQueuedAudio(SDL_AudioDeviceID) { played.clear(); }
const char* SDL_GetError() { return "injected audio failure"; }
int SDL_QueueAudio(SDL_AudioDeviceID, const void* samples, uint32_t bytes) {
    if (fail_queue) return -1;
    const auto* begin = static_cast<const int16_t*>(samples);
    played.insert(played.end(), begin, begin + bytes / 2);
    return 0;
}
uint32_t SDL_GetQueuedAudioSize(SDL_AudioDeviceID) { return played.size() * 2; }
uint64_t SDL_GetTicks64() { return ticks; }
int main() {
    try {
        wdc::audio::set_frequency(22000);
        wdc::audio::pump();
        require(opens == 1, "initial device open missing");
        wdc::audio::pump();
        require(opens == 1, "device retries every pump");
        ticks = 1000;
        fail_open = false;
        wdc::audio::pump();
        require(opens == 2, "transient device failure never recovers");
        int16_t swapped[]{200, 100, -400, -300};
        wdc::audio::queue_samples(swapped, 4);
        wdc::audio::pump();
        require(played.size() >= 4 && played[played.size()-4] == 100 &&
            played[played.size()-3] == 200 && played[played.size()-2] == -300 &&
            played.back() == -400, "stereo words were not converted");
        // A same-rate reset must discard audio already handed to SDL too.
        wdc::audio::set_frequency(22000);
        wdc::audio::pump();
        for (int16_t sample : played) require(sample == 0, "frequency reset retained stale device audio");
        std::vector<int16_t> block(2200, 123);
        for (int i = 0; i < 100; ++i) wdc::audio::queue_samples(block.data(), block.size());
        wdc::audio::pump();
        require(wdc::audio::frames_remaining() <= 22000 / 4, "host backlog is unbounded");
        fail_queue = true;
        wdc::audio::queue_samples(swapped, 4);
        wdc::audio::pump();
        require(wdc::audio::frames_remaining() == 0, "failed SDL submission kept stale queued frames");
        std::puts("audio output tests passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
