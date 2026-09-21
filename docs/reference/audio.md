# Audio reference

WDC's BOSS microcode combines graphics and audio in graphics tasks. Its measured
location and handshake are recorded in [peer research](../research/first-playable.md).
The game selects 22000 Hz during startup. The host receives signed 16-bit
stereo samples through the runtime's audio callbacks.

`aspMain.us.toml` translates BOSS, not a borrowed standalone audio microcode.
`src/audio.cpp` copies and reverses the halfwords in each word-swapped stereo
pair, queues host-owned buffers under a mutex, and opens/feeds the SDL device
only from the main-thread pump. Reported remaining frames combine pending
buffers with the most recent SDL queue-size sample. Audio quality still needs
listening acceptance; successful driving does not establish clean audio.
