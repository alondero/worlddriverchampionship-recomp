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

`src/audio.cpp` reverses the halfwords in each word-swapped signed stereo pair,
queues host-owned buffers under a mutex, and opens/feeds SDL only on the main
thread. A short silent lead absorbs graphics-frame timing jitter. Host output
is bounded to 250 ms; long stalls discard stale sound. Device-open and queue
failures retry after one second. Frequency changes discard both pending and
device audio, including resets to the same frequency. Host queue size is not
used as the guest DMA cursor.

Host tests exercise the circular-buffer submission cadence, deferred reads,
FIFO status/current length, fractional clock, idle/reset behavior, stereo byte
order, device recovery and bounded output. Captured output and successful
driving do not alone establish full audio fidelity; listening acceptance and
other tracks remain necessary.
