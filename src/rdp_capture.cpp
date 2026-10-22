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
std::string path;
unsigned captured = 0;

void fail() {
    std::fprintf(stderr, "[wdc] RDP capture failed writing %s; removed\n", path.c_str());
    std::fclose(file);
    std::remove(path.c_str());
    file = nullptr;
}
}

void request() {
    if (!prefix()) return;
    armed.store(true);
    std::fprintf(stderr, "[wdc] RDP capture armed for the next BOSS task\n");
}

void begin_task() {
    if (file || !armed.exchange(false)) return;
    // Numbering restarts with each process; an existing file is replaced.
    path = std::string(prefix()) + "-" + std::to_string(captured++) + ".rdp";
    file = std::fopen(path.c_str(), "wb");
    if (!file) { std::fprintf(stderr, "[wdc] RDP capture could not open %s\n", path.c_str()); return; }
    if (std::fputs("WDCRDP01", file) == EOF) return fail();
    std::fprintf(stderr, "[wdc] RDP capture writing %s\n", path.c_str());
}

void write(const uint8_t* memory, uint32_t start, uint32_t end) {
    if (!file) return;
    // [start, end) is BOSS's DP_START/DP_END command range in RDRAM or DMEM:
    // 8-byte aligned u32 words, word-swapped like all guest memory, bounds
    // checked by boss_rsp.cpp before the synchronous sink runs on the graphics
    // worker. The runtime owns the memory; it is only read, never retained.
    // Words are stored in N64 big-endian order. Any failed write discards the
    // whole capture rather than leaving a misaligned file.
    for (uint32_t pos = start; pos < end; pos += 4) {
        const uint32_t word = *reinterpret_cast<const uint32_t*>(memory + pos);
        const unsigned char bytes[4]{uint8_t(word >> 24), uint8_t(word >> 16), uint8_t(word >> 8), uint8_t(word)};
        if (std::fwrite(bytes, 1, 4, file) != 4) return fail();
    }
}

void end_task() {
    if (!file) return;
    const bool closed = std::fclose(file) == 0;
    file = nullptr;
    if (!closed) {
        std::remove(path.c_str());
        std::fprintf(stderr, "[wdc] RDP capture failed closing %s; removed\n", path.c_str());
        return;
    }
    std::fprintf(stderr, "[wdc] RDP capture complete\n");
}
}
