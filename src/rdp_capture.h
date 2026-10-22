#pragma once
#include <cstdint>

namespace wdc::rdp_capture {
// Development probe, disabled unless WDC_RDP_CAPTURE names an output prefix.
// request() runs on the SDL main thread and arms the next BOSS task. The
// graphics worker brackets each task with begin_task()/end_task() and passes
// every raw command range BOSS submits to write(). Captures are one-shot
// local evidence (tools/rdp/analyze_seams.py), never committed fixtures.
// Remove once RT64 can record raw RDP batches submitted by BOSS.
void request();
void begin_task();
void write(const uint8_t* memory, uint32_t start, uint32_t end);
void end_task();
}
