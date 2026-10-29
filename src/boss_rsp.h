#pragma once
#include <cstdint>
#include "ultramodern/ultra64.h"

namespace wdc::boss_capture { struct Capture; }

namespace wdc::rsp {
using RdpSink = void (*)(void*, uint8_t*, uint32_t, uint32_t);
// One graphics worker owns DMEM/RSP/DPC throughout a task. CPU status writes
// use atomics; event registrations use a mutex. Addresses are physical byte
// offsets in word-swapped guest RAM, checked before passing them to the sink.
void run(uint8_t* rdram, const OSTask* task, RdpSink sink, void* context);
void signal_dp_complete();
// Posts OS event `event` to the queue the game registered (boss_events.cpp).
void signal_game(unsigned event);

struct ReplayResult {
    bool finished = false;      // bossMain ended with BREAK
    bool reads_consumed = false; // every recorded SP_STATUS read was replayed
    uint64_t extra_reads = 0;   // status reads beyond the recording
};
// Offline tools only, never while the game runs: re-runs a captured task.
// `rdram` must hold capture.rdram (kRdramBytes, host layout) and is written
// as the real task wrote it. SP_STATUS reads return the recorded values and
// no game event is signalled. Not thread-safe with run().
ReplayResult replay(const boss_capture::Capture& capture, uint8_t* rdram, RdpSink sink, void* context);
}
