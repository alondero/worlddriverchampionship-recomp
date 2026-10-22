#include "rdp_capture.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace wdc::rdp_capture {
namespace {
const char* prefix() {
    static const char* value = std::getenv("WDC_RDP_CAPTURE");
    return value && *value ? value : nullptr;
}
std::atomic<bool> armed{false};
// Owned by the graphics worker between begin_task() and end_task().
std::FILE* file = nullptr;
unsigned captured = 0;
}

void request() {
    if (!prefix()) return;
    armed.store(true);
    std::fprintf(stderr, "[wdc] RDP capture armed for the next BOSS task\n");
}

void begin_task() {
    if (file || !armed.exchange(false)) return;
    const auto path = std::string(prefix()) + "-" + std::to_string(captured++) + ".rdp";
    file = std::fopen(path.c_str(), "wb");
    if (!file) { std::fprintf(stderr, "[wdc] RDP capture could not open %s\n", path.c_str()); return; }
    std::fputs("WDCRDP01", file);
    std::fprintf(stderr, "[wdc] RDP capture writing %s\n", path.c_str());
}

void write(const uint8_t* memory, uint32_t start, uint32_t end) {
    if (!file) return;
    // [start, end) is BOSS's DP_START/DP_END command range in RDRAM or DMEM:
    // 8-byte aligned u32 words, word-swapped like all guest memory, bounds
    // checked by boss_rsp.cpp before the synchronous sink runs on the graphics
    // worker. The runtime owns the memory; it is only read, never retained.
    // Words are stored in N64 big-endian order. A failed write truncates the
    // file; the analyser rejects partial words and skips a cut-off triangle.
    for (uint32_t pos = start; pos < end; pos += 4) {
        const uint32_t word = *reinterpret_cast<const uint32_t*>(memory + pos);
        const unsigned char bytes[4]{uint8_t(word >> 24), uint8_t(word >> 16), uint8_t(word >> 8), uint8_t(word)};
        std::fwrite(bytes, 1, 4, file);
    }
}

void end_task() {
    if (!file) return;
    std::fclose(file);
    file = nullptr;
    std::fprintf(stderr, "[wdc] RDP capture complete\n");
}
}
