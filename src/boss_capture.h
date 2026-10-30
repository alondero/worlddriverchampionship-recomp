#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wdc::boss_capture {
// One complete BOSS graphics task, recorded by src/boss_rsp.cpp so that
// wdc_boss_bench can re-run translated BOSS without the game, CPU or RT64.
// Memory images use the runtime's word-swapped host layout (guest physical
// byte a lives at index a ^ 3). Files and RdpRange::bytes use N64 big-endian
// order. Captures contain ROM-derived data and stay local.
inline constexpr uint32_t kRdramBytes = 0x800000, kDmemBytes = 0x1000;
// Upper bounds save() and load() both enforce on a capture file.
inline constexpr uint32_t kMaxStatusRuns = 1u << 24, kMaxRdpRanges = 1u << 20;
enum class Source : uint32_t { Rdram = 0, Dmem = 1 };

// Consecutive SP_STATUS COP0 reads that returned the same value.
struct StatusRun {
    uint32_t value = 0, reads = 0;
    bool operator==(const StatusRun&) const = default;
};
// One DP_START..DP_END command range BOSS handed to the RDP sink.
struct RdpRange {
    Source source = Source::Rdram;
    uint32_t start = 0, end = 0;
    std::vector<uint8_t> bytes;
    bool operator==(const RdpRange&) const = default;
};

struct Capture {
    std::array<uint32_t, 16> task{};  // OSTask words as passed to the worker
    uint32_t sp_status = 0;           // SP_STATUS as bossMain starts
    std::array<uint32_t, 4> dp{};     // DP_START, DP_END, DP_CURRENT, DP_STATUS
    std::vector<uint8_t> dmem;        // kDmemBytes after the task prelude
    // kRdramBytes as BOSS read it: RDRAM after the task (CPU sublists in
    // place) with every byte BOSS wrote restored to its pre-write value.
    std::vector<uint8_t> rdram;
    std::vector<StatusRun> status_reads;
    std::vector<RdpRange> output;
};

// Copies [start, end) of host-layout `memory` into a big-endian range.
RdpRange copy_range(Source source, const uint8_t* memory, uint32_t start, uint32_t end);
void record_status(std::vector<StatusRun>& runs, uint32_t value);

// Replays recorded SP_STATUS reads in order. BOSS's dispatcher polls SIG0
// for the CPU's "sublist complete" signal, so a replay must observe the same
// sequence of values the real task observed, not a live register.
class StatusPlayback {
public:
    explicit StatusPlayback(const std::vector<StatusRun>& runs) : runs(&runs) {}
    // The next recorded value, or `fallback` (counted) once exhausted.
    uint32_t next(uint32_t fallback);
    bool exhausted() const;
    uint64_t overruns() const { return extra; }
private:
    const std::vector<StatusRun>* runs;
    size_t index = 0;
    uint32_t used = 0;
    uint64_t extra = 0;
};

// Remembers the value of each RDRAM byte before BOSS first wrote it during
// one task, so the task's input view can be rebuilt after it finishes.
class WriteJournal {
public:
    WriteJournal() : written(kRdramBytes) {}
    // Call before BOSS writes guest physical bytes [address, address + bytes)
    // of host-layout `rdram`. Returns false for a range outside kRdramBytes.
    bool before_write(const uint8_t* rdram, uint32_t address, uint32_t bytes);
    void restore(std::vector<uint8_t>& image) const;
    size_t size() const { return original.size(); }
private:
    std::vector<bool> written;
    std::vector<std::pair<uint32_t, uint8_t>> original; // host index, value
};

bool save(const Capture& capture, const std::string& path, std::string& error);
bool load(const std::string& path, Capture& capture, std::string& error);

// Development probe, disabled unless WDC_BOSS_CAPTURE names an output prefix.
// request() runs on the SDL main thread (F10, alongside WDC_RDP_CAPTURE);
// take_request() runs on the graphics worker and yields <prefix>-<n>.boss.
void request();
bool take_request(std::string& path);
}
