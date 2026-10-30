#include "boss_rsp.h"
#include "boss_capture.h"
#include "boss_dma.h"
#include "guest_memory.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

extern RspExitReason bossMain(uint8_t*, uint32_t);
extern "C" [[noreturn]] void wdc_unimplemented(const char*);

namespace {
namespace capture = wdc::boss_capture;
std::atomic<uint32_t> status{1};
uint32_t dp_start{}, dp_end{}, dp_current{}, dp_status{};
uint8_t* memory;
wdc::rsp::RdpSink rdp_sink;
void* rdp_context;

// WDC_BOSS_CAPTURE: the live task being recorded. Everything here belongs to
// the graphics worker: run(), the BOSS COP0/DMA callbacks, and RT64's DP
// interrupt callback (raised inside processDisplayLists on the same thread)
// all run there. DP-complete is held until the task's RDRAM view is saved,
// because once it is signalled the game starts writing the next frame's
// lists over data BOSS already read. Mid-task SP signals still go out: the
// CPU's ZSort stage must answer them for BOSS to finish. A scene whose CPU
// waited for DP-complete before answering would stall while recording; the
// Hawaii racing tasks recorded so far do not.
struct Recording {
    capture::Capture capture;
    capture::WriteJournal journal;
    std::string path;
    bool outside_rdram = false;
};
std::unique_ptr<Recording> recording;
bool holding_dp = false;
unsigned held_dp = 0;
// Offline replay: recorded status reads and a private status register.
capture::StatusPlayback* playback = nullptr;
std::atomic<uint32_t> replay_status{0};
// The game's OS event sink. The player installs it on the main thread before
// the graphics worker starts; an offline tool installs none, so a replay cannot
// signal the game. The pointer is read on the graphics worker, so it is
// atomic: the install happens-before the worker runs, and the atomic keeps the
// read free of a data race if the install is ever moved later.
std::atomic<wdc::rsp::GameEventSink> game_event_sink{nullptr};

// Call the installed sink, if there is one. A missing sink is not an error:
// it is how an offline tool says "there is no game to signal".
void call_sink(unsigned event) {
    if (const auto sink = game_event_sink.load(); sink) sink(event);
}

void write_status(uint32_t value, bool from_rsp) {
    auto& reg = playback ? replay_status : status;
    uint32_t old = reg.load(), next;
    do {
        next = old;
        if (value & 1) next &= ~1u;
        if (value & 2) next |= 1u;
        if (value & 4) next &= ~2u;
        for (unsigned i = 0; i < 8; ++i) {
            if (value & (0x200u << (i * 2))) next &= ~(0x80u << i);
            if (value & (0x400u << (i * 2))) next |= 0x80u << i;
        }
    } while (!reg.compare_exchange_weak(old, next));
    if (from_rsp && (value & 0x10)) {
        // ROM handler 0x8007522C clears SIG3 with 0x8008, then dispatches
        // OS_EVENT_SP if SIG1/2 are set, otherwise OS_EVENT_SP_BREAK.
        reg.fetch_and(~0x400u);
        if (!playback) call_sink((next & 0x300) ? 4 : 11);
    }
}

void begin_recording(const OSTask* task) {
    auto& start = recording->capture;
    static_assert(sizeof(OSTask) == sizeof(start.task));
    std::memcpy(start.task.data(), task, sizeof(start.task));
    start.sp_status = status.load();
    start.dp = {dp_start, dp_end, dp_current, dp_status};
    start.dmem.assign(dmem, dmem + capture::kDmemBytes);
}

// Copies the runtime-owned, word-swapped 8 MiB RDRAM (host index a ^ 3 for
// physical byte a) on the graphics worker, before the task's completion
// signals are released. Guest threads that are not waiting on this frame
// keep running; a write of theirs that BOSS had already read makes a capture
// that wdc_boss_bench rejects, never one that passes. The file is written
// after the signals, so only the save, not the game, waits on the disk.
void finish_recording(const uint8_t* rdram, unsigned final_event) {
    const auto rec = std::move(recording);
    auto& task = rec->capture;
    task.rdram.assign(rdram, rdram + capture::kRdramBytes);
    rec->journal.restore(task.rdram);
    holding_dp = false;
    for (; held_dp; --held_dp) call_sink(9);
    call_sink(final_event);
    if (rec->outside_rdram) {
        std::fprintf(stderr, "[wdc] BOSS capture discarded: DMA write outside 8 MiB RDRAM\n");
        return;
    }
    std::string error;
    if (!capture::save(task, rec->path, error)) {
        std::fprintf(stderr, "[wdc] BOSS capture failed: %s\n", error.c_str());
        return;
    }
    std::fprintf(stderr, "[wdc] BOSS capture wrote %s: %zu status runs, %zu RDP ranges, %zu bytes journalled\n",
        rec->path.c_str(), task.status_reads.size(), task.output.size(), rec->journal.size());
}
}

extern "C" void wdc_sp_yield(uint8_t*, recomp_context*) { write_status(0x400, false); }
extern "C" void wdc_sp_set_status(uint8_t*, recomp_context* ctx) { write_status(ctx->r4, false); }

void wdc::rsp::set_game_event_sink(GameEventSink sink) { game_event_sink.store(sink); }

// Generated BOSS writes RDRAM only here (src/boss_dma.h redirects every
// DO_DMA_WRITE), on the graphics worker during run() or an offline replay.
// The runtime helper performs the unchanged transfer: whole 8-byte words of
// word-swapped guest RAM, physical address masked to 24 bits. While
// recording, the same range is journalled first. A range outside the
// journal's 8 MiB still reaches guest memory, but the capture can no longer
// rebuild its input view, so the capture is dropped and the game carries on.
// Remove with the capture probe.
void wdc_boss_dma_write(uint8_t* rdram, uint32_t dmem_addr, uint32_t dram_addr, uint32_t wr_len) {
    if (recording) {
        const auto range = wdc_boss_dma_write_range(dram_addr, wr_len);
        if (!recording->journal.before_write(rdram, range.address, range.bytes)) recording->outside_rdram = true;
    }
    dma_dmem_to_rdram(rdram, dmem_addr, dram_addr, wr_len);
}

uint32_t rsp_read_cop0(uint32_t reg) {
    switch (reg) {
    case 4: {
        if (playback) return playback->next(replay_status.load());
        std::this_thread::yield();
        const uint32_t value = status.load();
        if (recording) capture::record_status(recording->capture.status_reads, value);
        return value;
    }
    case 5: case 6: case 7: return 0;
    case 8: return dp_start;
    case 9: return dp_end;
    case 10: return dp_current;
    case 11: return dp_status;
    default: wdc_unimplemented("BOSS COP0 read");
    }
}

void rsp_write_cop0(uint32_t reg, uint32_t value) {
    switch (reg) {
    case 4: write_status(value, true); return;
    case 7: return;
    case 8: dp_start = dp_current = value & 0xFFFFF8; return;
    case 9: {
        dp_end = value & 0xFFFFF8;
        const bool xbus = dp_status & 1; // commands come from DMEM
        auto* source = xbus ? dmem : memory;
        const uint32_t limit = xbus ? 0x1000 : 0x800000;
        if (dp_current > dp_end || dp_end > limit) wdc_unimplemented("BOSS RDP range");
        if (dp_current != dp_end) {
            if (recording) {
                recording->capture.output.push_back(capture::copy_range(
                    xbus ? capture::Source::Dmem : capture::Source::Rdram, source, dp_current, dp_end));
            }
            rdp_sink(rdp_context, source, dp_current, dp_end);
        }
        dp_current = dp_end;
        return;
    }
    case 11:
        if (value & 1) dp_status &= ~1u;
        if (value & 2) dp_status |= 1u;
        return;
    default: wdc_unimplemented("BOSS COP0 write");
    }
}

void wdc::rsp::signal_dp_complete() {
    if (holding_dp) ++held_dp;
    else call_sink(9);
}

void wdc::rsp::run(uint8_t* rdram, const OSTask* task, RdpSink sink, void* context) {
    if ((task->t.ucode & 0x1FFFFFFF) != 0x7C780 || task->t.ucode_size != 0xF60)
        wdc_unimplemented("unidentified BOSS microcode");
    memory = rdram;
    rdp_sink = sink;
    rdp_context = context;
    std::string path;
    if (capture::take_request(path)) {
        recording = std::make_unique<Recording>();
        recording->path = std::move(path);
        holding_dp = true;
    }
    std::memcpy(dmem + 0xFC0, task, sizeof(*task));
    // Task data is borrowed for this synchronous graphics-worker execution;
    // the runtime owns RDRAM and serializes BOSS tasks. Preserve word swapping.
    // Remove this bridge when upstream offers an equivalent BOSS LLE backend.
    wdc_require_guest_range(uint32_t(task->t.ucode_data) | 0x80000000u, 0xF80, 8);
    dma_rdram_to_dmem(rdram, 0, task->t.ucode_data, 0xF80 - 1);
    status.fetch_and(~3u);
    if (recording) begin_recording(task);
    auto result = bossMain(rdram, task->t.ucode);
    if (result != RspExitReason::Broke) wdc_unimplemented("BOSS did not finish");
    status.fetch_or(3);
    // BREAK interrupt follows the ROM's SIG1/2 classification too.
    const unsigned final_event = (status.load() & 0x300) ? 4 : 11;
    if (recording) finish_recording(rdram, final_event);
    else call_sink(final_event);
}

wdc::rsp::ReplayResult wdc::rsp::replay(const capture::Capture& recorded, uint8_t* rdram, RdpSink sink, void* context) {
    if (recorded.dmem.size() != capture::kDmemBytes) wdc_unimplemented("BOSS replay without DMEM");
    memory = rdram;
    rdp_sink = sink;
    rdp_context = context;
    std::memcpy(dmem, recorded.dmem.data(), capture::kDmemBytes);
    dp_start = recorded.dp[0]; dp_end = recorded.dp[1]; dp_current = recorded.dp[2]; dp_status = recorded.dp[3];
    replay_status = recorded.sp_status;
    capture::StatusPlayback reads(recorded.status_reads);
    playback = &reads;
    OSTask task;
    std::memcpy(&task, recorded.task.data(), sizeof(task));
    const auto result = bossMain(rdram, task.t.ucode);
    playback = nullptr;
    return {result == RspExitReason::Broke, reads.exhausted(), reads.overruns()};
}
