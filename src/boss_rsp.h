#pragma once
#include <cstdint>
#include "ultramodern/ultra64.h"

namespace wdc::rsp {
using RdpSink = void (*)(void*, uint8_t*, uint32_t, uint32_t);
// One graphics worker owns DMEM/RSP/DPC throughout a task. CPU status writes
// use atomics; event registrations use a mutex. Addresses are physical byte
// offsets in word-swapped guest RAM, checked before passing them to the sink.
void run(uint8_t* rdram, const OSTask* task, RdpSink sink, void* context);
void signal_dp_complete();
}
