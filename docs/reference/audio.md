# Audio reference

WDC's BOSS microcode combines graphics and audio in graphics tasks. Its measured
location and handshake are recorded in [peer research](../research/first-playable.md).
The game selects 22000 Hz during startup. The host receives signed 16-bit
stereo samples through the runtime's audio callbacks.

`aspMain.us.toml` translates BOSS, not a borrowed standalone audio microcode.
The SDK AI entries in `rom-symbols.toml` use the port's `src/audio_ai.cpp`
hooks. WDC submits its 0x4000-byte circular buffer at 0x803F0400 from
`func_80042FA8` and derives the next write position from the current DMA's
remaining byte count. Treating each graphics-frame submission as an immediately
completed, copied buffer causes an ever-growing backlog and reads samples
before BOSS fills them.

`src/audio_dma.cpp` models two hardware DMA descriptors and an elapsed sample
clock. Status reports busy/full; length reports only the current descriptor in
8-byte units. Polling AI copies the elapsed portion of runtime-owned RDRAM
synchronously on the calling guest thread, before the next BOSS ring update.
It retains addresses rather than host pointers. A frequency reset discards DMA
state. The existing runtime dispatches AI events at VI boundaries.

BOSS synthesizes 349-frame blocks during the intro, writing each block to the
ring as 512, 512, and 372-byte RSP DMA requests. Every second block begins four
bytes into an eight-byte word. The hardware aligns DRAM addresses down and
rounds inclusive DMA lengths up to eight bytes; a request with length register
`0x173` transfers 376 bytes. The pinned runtime only added one to that register,
copying 372 bytes. This left one stereo frame from the previous ring lap every
698 frames, producing periodic clicks already present in PCM before SDL.

Patch 0018 fixes rounding in both RSP DMA directions. The addressing and guest
AI clock are unchanged. The current upstream
[runtime helper](https://github.com/N64Recomp/N64ModernRuntime/blob/main/librecomp/include/librecomp/rsp.hpp)
has the same short-copy behavior; the reference
[ares register handling](https://github.com/ares-emulator/ares/blob/master/ares/n64/rsp/io.cpp)
and [DMA loop](https://github.com/ares-emulator/ares/blob/master/ares/n64/rsp/dma.cpp)
transfer whole eight-byte words. `wdc_rsp_dma` reproduces the two-block pattern
with synthetic word-swapped memory and checks both transfer directions, short
lengths, full-page transfers, address masking and guard bytes.

Verification used the USA ROM and a captured intro BOSS task: replaying it with
rounded DMA lengths corrected exactly the missing stereo frame in the output
ring. In the first 40 seconds of captured PCM, the original player had 168
isolated impulses on one 698-frame phase; the patched player had none under
the same check (opposite adjacent differences above 800 s16 units and six
times the surrounding 32-frame difference RMS). This is a signal diagnostic,
not a physical-output listening comparison. These checks distinguish
synthesis corruption from host starvation;
long scheduling stalls can still exhaust the playback reserve described below.

`src/audio.cpp` reverses the halfwords in each word-swapped signed stereo pair,
queues host-owned buffers under a mutex, and opens/feeds SDL only on the main
thread. Playback starts paused and waits for 100 ms of actual PCM or three
obtained SDL callback periods, whichever is larger (capped at 200 ms to leave
room for a 30 Hz batch below the 250 ms backlog ceiling). An empty
playing queue pauses and rebuilds the same reserve before resuming. Synthetic
silence is not inserted between PCM blocks. Host output is bounded to 250 ms
at each pump; long stalls discard stale sound. Device-open and queue
failures retry after one second. Frequency changes discard both pending and
device audio, including resets to the same frequency. Host queue size is not
used as the guest DMA cursor.

The SDL device accepts the guest's s16 stereo rate with no allowed format/rate
changes. SDL owns any persistent conversion to the physical endpoint; the port
does not create a fresh resampler per DMA fragment. Windows defaults to WASAPI
before SDL initialization, while honoring an explicit `SDL_AUDIODRIVER`.
`WDC_AUDIO_STATS=1` logs device period/reserve, detected empty queues, playback
starts and backlog discards. These are host queue observations, not a measure
of physical speaker latency or proof of synthesis fidelity.

Peer comparison: [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/src/main/main.cpp)
and [BanjoRecomp](https://github.com/BanjoRecomp/BanjoRecomp/blob/main/src/main/main.cpp)
use a 48 kHz host device, preserve boundary samples for their chunk resampler,
reserve audio in feedback, bound accumulated latency and select WASAPI on
Windows because of DirectSound queueing issues. AeroGauge's `src/aero_audio.cpp`
uses a persistent SDL conversion stream and waits for 100 ms or three device
periods of PCM at startup and after starvation. WDC adopts that buffering
policy, retaining its own DMA cursor because BOSS streams a circular buffer
inside graphics tasks rather than submitting standalone audio tasks.

Host tests exercise the circular-buffer submission cadence, deferred reads,
FIFO status/current length, fractional clock, idle/reset behavior, stereo byte
order, device recovery and bounded output. The output test feeds the real DMA
clock into the host sink at 30 Hz while a simulated device consumes independent
512/1024-frame periods at 22000/48000 Hz. Twenty-second runs include recurring
80 ms main-thread delays; a separate starvation case checks that tiny fragments
cannot restart playback before the PCM reserve is restored. A 2048-frame
startup case checks that a capped reserve remains reachable by 30 Hz batches.
The device is stubbed, so these tests do not validate SDL's resampler or physical
playback. Captured output and successful driving do not alone establish full
audio fidelity; listening acceptance and other tracks remain necessary.
