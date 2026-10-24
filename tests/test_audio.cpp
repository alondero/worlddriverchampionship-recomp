#include "audio.h"
#include "audio_dma.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
uint64_t ticks;
unsigned opens;
bool fail_open = true, fail_queue;
bool paused = true;
uint16_t callback_frames = 512;
unsigned starts, underruns;
std::vector<int16_t> played;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void playback(uint32_t rate, uint16_t period) {
    wdc::audio::set_frequency(0);
    wdc::audio::pump();
    callback_frames = period;
    wdc::audio::set_frequency(rate);
    wdc::audio::pump();
    starts = underruns = 0;
    wdc::audio::Dma dma;
    dma.reset(rate);
    // Use the production DMA clock and WDC's two descriptors over one ring.
    std::vector<int16_t> ring(0x4000 / 2, 123);
    auto sink = [](void* context, uint32_t address, uint32_t bytes) {
        auto& ring = *static_cast<std::vector<int16_t>*>(context);
        wdc::audio::queue_samples(ring.data() + (address - 0x803F0400) / 2, bytes / 2);
    };
    unsigned submissions = 0, callbacks = 0;
    for (unsigned ms = 0; ms < 20000; ++ms) {
        if (ms * 30 >= submissions * 1000) {
            dma.advance(submissions ? 33333333 : 0, sink, &ring);
            if (!(dma.status() & 0x80000000)) require(dma.enqueue(0x803F0400, 0x4000), "DMA enqueue failed");
            ++submissions;
        }
        // SDL's main thread can be late even while guest audio keeps arriving.
        // Supply 80 ms batches every second, otherwise pump every 5 ms.
        if (ms % 5 == 0 && ms % 1000 >= 80) {
            wdc::audio::pump();
            require(wdc::audio::frames_remaining() <= rate / 4, "playback backlog exceeds 250 ms");
        }
        if (uint64_t(ms) * rate >= uint64_t(callbacks) * period * 1000) {
            ++callbacks;
            if (!paused) {
                const size_t samples = period * 2;
                if (played.size() < samples) ++underruns;
                played.erase(played.begin(), played.begin() + std::min(played.size(), samples));
            }
        }
    }
    require(starts == 1 && underruns == 0, "jittery 30 Hz DMA playback starves the host device");
    // A long stall is unavoidable, but recovery must build a real PCM reserve.
    played.clear();
    int16_t tiny[]{200, 100, -400, -300};
    wdc::audio::queue_samples(tiny, 4);
    wdc::audio::pump();
    require(paused && starts == 1, "starved device resumed before rebuilding its reserve");
    require(played.size() == 4 && played[0] == 100 && played[1] == 200,
        "recovery inserted silence or lost stereo PCM");
    std::vector<int16_t> refill(rate / 5 * 2, 123);
    wdc::audio::queue_samples(refill.data(), refill.size());
    wdc::audio::pump();
    require(!paused && starts == 2, "starved device never resumed");
    std::printf("playback %u Hz / %u frames: %u steady underruns; recovery passed\n", rate, period, underruns);
}
void large_period_startup() {
    wdc::audio::set_frequency(0);
    wdc::audio::pump();
    callback_frames = 2048; // Three periods exceed the 250 ms backlog ceiling.
    wdc::audio::set_frequency(22000);
    wdc::audio::pump();
    std::vector<int16_t> block(733 * 2, 123); // One 30 Hz game update.
    for (unsigned i = 0; i < 30 && paused; ++i) {
        wdc::audio::queue_samples(block.data(), block.size());
        wdc::audio::pump();
        require(wdc::audio::frames_remaining() <= 5500, "large-period startup exceeded queue limit");
    }
    require(!paused, "large callback period can never reach the capped startup reserve");
}
}
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*, int, const SDL_AudioSpec* spec, SDL_AudioSpec* obtained, int changes) {
    ++opens;
    require(spec->format == AUDIO_S16SYS && spec->channels == 2, "device format changed");
    require(changes == SDL_AUDIO_ALLOW_SAMPLES_CHANGE, "device period changes were not allowed or PCM units changed");
    if (obtained) { *obtained = *spec; obtained->samples = callback_frames; }
    paused = true;
    return fail_open ? 0 : 1;
}
void SDL_CloseAudioDevice(SDL_AudioDeviceID) { played.clear(); }
void SDL_PauseAudioDevice(SDL_AudioDeviceID, int pause) {
    paused = pause != 0;
    if (!paused) ++starts;
}
void SDL_ClearQueuedAudio(SDL_AudioDeviceID) { played.clear(); }
const char* SDL_GetError() { return "injected audio failure"; }
int SDL_QueueAudio(SDL_AudioDeviceID, const void* samples, uint32_t bytes) {
    if (fail_queue) return -1;
    const auto* begin = static_cast<const int16_t*>(samples);
    played.insert(played.end(), begin, begin + bytes / 2);
    return 0;
}
uint32_t SDL_GetQueuedAudioSize(SDL_AudioDeviceID) { return static_cast<uint32_t>(played.size() * 2); }
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
        require(paused && played.size() == 4, "startup played a tiny fragment or inserted silence");
        require(played.size() >= 4 && played[played.size()-4] == 100 &&
            played[played.size()-3] == 200 && played[played.size()-2] == -300 &&
            played.back() == -400, "stereo words were not converted");
        // A same-rate reset must discard audio already handed to SDL too.
        wdc::audio::set_frequency(22000);
        wdc::audio::pump();
        require(paused && played.empty(), "frequency reset retained stale audio or kept playback running");
        std::vector<int16_t> block(2200, 123);
        for (int i = 0; i < 100; ++i) wdc::audio::queue_samples(block.data(), block.size());
        wdc::audio::pump();
        require(wdc::audio::frames_remaining() <= 22000 / 4, "host backlog is unbounded");
        require(!paused, "full PCM reserve did not start playback");
        wdc::audio::set_frequency(22000);
        wdc::audio::pump();
        require(paused && played.empty(), "same-rate reset did not stop running playback");
        fail_queue = true;
        wdc::audio::queue_samples(swapped, 4);
        wdc::audio::pump();
        require(wdc::audio::frames_remaining() == 0, "failed SDL submission kept stale queued frames");
        fail_queue = false;
        ticks += 1000;
        playback(22000, 512);
        playback(48000, 1024);
        playback(22000, 1024);
        large_period_startup();
        wdc::audio::set_frequency(0);
        wdc::audio::pump();
        require(wdc::audio::frames_remaining() == 0, "disabled device retained audio");
        std::puts("audio output tests passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
