// The BOSS replay contract, without a ROM.
//
// src/boss_rsp.cpp restores the captured DMEM, DP registers and SP_STATUS,
// replays the recorded status reads in order, and hands every DP range to
// the sink. The translated microcode is replaced here by a script, so the
// test can ask for exactly the reads and ranges it needs and check what the
// device did with them.

#include "boss_capture.h"
#include "boss_rsp.h"
#include "librecomp/rsp.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern uint32_t rsp_read_cop0(uint32_t reg);
extern void rsp_write_cop0(uint32_t reg, uint32_t value);

namespace capture = wdc::boss_capture;

namespace {
struct Script {
    std::vector<uint32_t> reads;         // status values the script expects
    std::vector<uint32_t> observed;      // what the device returned
    std::vector<uint32_t> dp_registers;  // DP registers read back
    uint32_t dp_start = 0, dp_end = 0;
    bool range = false;                  // emit one DP range
    bool mismatch = false;               // a status read did not match
    RspExitReason exit_reason = RspExitReason::Broke;
};
Script script;
unsigned game_events = 0;

struct Sink {
    unsigned ranges = 0;
    bool from_dmem = false;
    uint32_t start = 0, end = 0;
    std::vector<uint8_t> bytes;
};

void count_events(unsigned) { ++game_events; }

void sink(void* context, uint8_t* memory, uint32_t start, uint32_t end) {
    auto& state = *static_cast<Sink*>(context);
    ++state.ranges;
    state.from_dmem = memory == dmem;
    state.start = start;
    state.end = end;
    state.bytes.assign(memory + start, memory + end);
}
}

extern "C" [[noreturn]] void wdc_unimplemented(const char* name) {
    std::fprintf(stderr, "unexpected unsupported operation: %s\n", name);
    std::exit(1);
}

// Stands in for the generated microcode (src/aspMain.cpp), which needs a ROM.
RspExitReason bossMain(uint8_t* rdram, uint32_t ucode_addr) {
    (void)rdram;
    (void)ucode_addr;
    for (uint32_t expected : script.reads) {
        const uint32_t got = rsp_read_cop0(4);
        script.observed.push_back(got);
        if (got != expected) script.mismatch = true;
    }
    for (uint32_t reg : {8u, 9u, 10u, 11u}) script.dp_registers.push_back(rsp_read_cop0(reg));
    if (script.range) {
        rsp_write_cop0(8, script.dp_start);
        rsp_write_cop0(9, script.dp_end);
    }
    // The SP signal path: a replay must not signal the game.
    rsp_write_cop0(4, 0x10);
    return script.exit_reason;
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

// A capture with a known DMEM, RDRAM and DP state.
static capture::Capture make_capture() {
    capture::Capture c;
    c.task[0] = 0x80000000u;
    c.sp_status = 0x300;
    c.dp = {0x100, 0x180, 0x100, 0};
    c.dmem.assign(capture::kDmemBytes, 0);
    c.rdram.assign(capture::kRdramBytes, 0);
    for (uint32_t a = 0; a < capture::kDmemBytes; ++a) c.dmem[a ^ 3] = uint8_t(a * 7 + 1);
    for (uint32_t a = 0; a < 0x200; ++a) c.rdram[(0x100 + a) ^ 3] = uint8_t(a + 3);
    return c;
}

int main() {
    // The device posts OS events through the installed sink; the replay must
    // not use it.
    wdc::rsp::set_game_event_sink(&count_events);

    // The recorded status reads are replayed in order, the captured DP and
    // DMEM state is restored, and the DP range reaches the sink unchanged.
    {
        game_events = 0;
        auto c = make_capture();
        c.status_reads = {{0x80, 2}, {0, 1}};
        script = {};
        script.reads = {0x80, 0x80, 0x00};
        script.range = true;
        script.dp_start = 0x100;
        script.dp_end = 0x180;
        std::vector<uint8_t> rdram = c.rdram;
        Sink state;
        const auto result = wdc::rsp::replay(c, rdram.data(), sink, &state);
        require(result.finished, "the replay did not finish");
        require(result.reads_consumed, "the replay did not consume every recorded status read");
        require(result.extra_reads == 0, "the replay read past the recording");
        require(!script.mismatch, "the replay returned a status read that was not recorded");
        require(script.observed == std::vector<uint32_t>{0x80, 0x80, 0x00}, "the recorded status reads were replayed out of order");
        require(script.dp_registers == std::vector<uint32_t>{0x100, 0x180, 0x100, 0}, "the captured DP registers were not restored");
        require(std::memcmp(dmem, c.dmem.data(), capture::kDmemBytes) == 0, "the captured DMEM was not restored");
        require(state.ranges == 1, "the DP range did not reach the sink");
        require(!state.from_dmem, "the sink was given DMEM for an RDRAM range");
        require(state.start == 0x100 && state.end == 0x180, "the sink was given the wrong DP range");
        require(state.bytes == std::vector<uint8_t>(c.rdram.begin() + 0x100, c.rdram.begin() + 0x180),
            "the sink was given the wrong bytes");
        require(game_events == 0, "the replay signalled the game");
    }

    // A task that reads past the recording gets the captured status back and
    // is reported as an overrun, so a short capture cannot pass unnoticed.
    {
        game_events = 0;
        auto c = make_capture();
        c.status_reads = {{0x80, 1}};
        script = {};
        script.reads = {0x80, 0x300, 0x300};
        std::vector<uint8_t> rdram = c.rdram;
        Sink state;
        const auto result = wdc::rsp::replay(c, rdram.data(), sink, &state);
        require(result.finished, "the replay did not finish");
        require(result.reads_consumed, "the replay did not consume the recorded status read");
        require(result.extra_reads == 2, "the replay did not count the reads past the recording");
        require(script.observed == std::vector<uint32_t>{0x80, 0x300, 0x300}, "the replay did not fall back to the captured status");
        require(game_events == 0, "the replay signalled the game");
    }

    // A task that reads less than the recording leaves the recording
    // unfinished, which the caller must be able to see.
    {
        game_events = 0;
        auto c = make_capture();
        c.status_reads = {{0x80, 3}};
        script = {};
        script.reads = {0x80};
        std::vector<uint8_t> rdram = c.rdram;
        Sink state;
        const auto result = wdc::rsp::replay(c, rdram.data(), sink, &state);
        require(result.finished, "the replay did not finish");
        require(!result.reads_consumed, "the replay consumed a status read it never made");
        require(result.extra_reads == 0, "the replay counted a read it never made");
        require(game_events == 0, "the replay signalled the game");
    }

    // A task that does not end with BREAK is not a finished replay.
    {
        game_events = 0;
        auto c = make_capture();
        script = {};
        script.exit_reason = RspExitReason::Invalid;
        std::vector<uint8_t> rdram = c.rdram;
        Sink state;
        const auto result = wdc::rsp::replay(c, rdram.data(), sink, &state);
        require(!result.finished, "the replay reported a task that did not finish");
        require(game_events == 0, "the replay signalled the game");
    }

    // A range whose commands come from DMEM is handed to the sink from DMEM.
    {
        game_events = 0;
        auto c = make_capture();
        c.dp = {0x0, 0x40, 0x0, 1};
        script = {};
        script.range = true;
        script.dp_start = 0x0;
        script.dp_end = 0x40;
        std::vector<uint8_t> rdram = c.rdram;
        Sink state;
        const auto result = wdc::rsp::replay(c, rdram.data(), sink, &state);
        require(result.finished, "the replay did not finish");
        require(state.ranges == 1, "the DP range did not reach the sink");
        require(state.from_dmem, "the sink was given RDRAM for a DMEM range");
        require(state.start == 0x0 && state.end == 0x40, "the sink was given the wrong DP range");
        require(state.bytes == std::vector<uint8_t>(c.dmem.begin(), c.dmem.begin() + 0x40),
            "the sink was given the wrong bytes");
        require(game_events == 0, "the replay signalled the game");
    }

    std::puts("BOSS replay contract passed");
}
