#include "boss_rsp.h"
#include "guest_memory.h"
#include "librecomp/rsp.hpp"
#include "ultramodern/ultramodern.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

extern RspExitReason bossMain(uint8_t*, uint32_t);
extern "C" [[noreturn]] void wdc_unimplemented(const char*);
extern "C" void osSetEventMesg_recomp(uint8_t*, recomp_context*);

namespace {
struct Event { int32_t queue = 0; int32_t message = 0; };
std::array<Event, 16> events;
std::mutex event_mutex;
std::atomic<uint32_t> status{1};
uint32_t dp_start{}, dp_end{}, dp_current{}, dp_status{};
uint8_t* memory;
wdc::rsp::RdpSink rdp_sink;
void* rdp_context;

void signal(unsigned event) {
    Event target;
    { std::lock_guard lock(event_mutex); target = events.at(event); }
    if (target.queue) ultramodern::enqueue_external_message(target.queue, target.message, false, false);
}

void write_status(uint32_t value, bool from_rsp) {
    uint32_t old = status.load(), next;
    do {
        next = old;
        if (value & 1) next &= ~1u;
        if (value & 2) next |= 1u;
        if (value & 4) next &= ~2u;
        for (unsigned i = 0; i < 8; ++i) {
            if (value & (0x200u << (i * 2))) next &= ~(0x80u << i);
            if (value & (0x400u << (i * 2))) next |= 0x80u << i;
        }
    } while (!status.compare_exchange_weak(old, next));
    if (from_rsp && (value & 0x10)) {
        // ROM handler 0x8007522C clears SIG3 with 0x8008, then dispatches
        // OS_EVENT_SP if SIG1/2 are set, otherwise OS_EVENT_SP_BREAK.
        status.fetch_and(~0x400u);
        signal((next & 0x300) ? 4 : 11);
    }
}
}

extern "C" void wdc_set_event(uint8_t* rdram, recomp_context* ctx) {
    const auto event = static_cast<unsigned>(ctx->r4);
    if (event < events.size()) {
        std::lock_guard lock(event_mutex);
        events[event] = {static_cast<int32_t>(ctx->r5), static_cast<int32_t>(ctx->r6)};
    }
    osSetEventMesg_recomp(rdram, ctx);
}
extern "C" void wdc_sp_yield(uint8_t*, recomp_context*) { write_status(0x400, false); }
extern "C" void wdc_sp_set_status(uint8_t*, recomp_context* ctx) { write_status(ctx->r4, false); }

uint32_t rsp_read_cop0(uint32_t reg) {
    switch (reg) {
    case 4: std::this_thread::yield(); return status.load();
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
        auto* source = (dp_status & 1) ? dmem : memory;
        const uint32_t limit = (dp_status & 1) ? 0x1000 : 0x800000;
        if (dp_current > dp_end || dp_end > limit) wdc_unimplemented("BOSS RDP range");
        if (dp_current != dp_end) rdp_sink(rdp_context, source, dp_current, dp_end);
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

void wdc::rsp::signal_dp_complete() { signal(9); }

void wdc::rsp::run(uint8_t* rdram, const OSTask* task, RdpSink sink, void* context) {
    if ((task->t.ucode & 0x1FFFFFFF) != 0x7C780 || task->t.ucode_size != 0xF60)
        wdc_unimplemented("unidentified BOSS microcode");
    memory = rdram;
    rdp_sink = sink;
    rdp_context = context;
    std::memcpy(dmem + 0xFC0, task, sizeof(*task));
    // Task data is borrowed for this synchronous graphics-worker execution;
    // the runtime owns RDRAM and serializes BOSS tasks. Preserve word swapping.
    // Remove this bridge when upstream offers an equivalent BOSS LLE backend.
    wdc_require_guest_range(uint32_t(task->t.ucode_data) | 0x80000000u, 0xF80, 8);
    dma_rdram_to_dmem(rdram, 0, task->t.ucode_data, 0xF80 - 1);
    status.fetch_and(~3u);
    auto result = bossMain(rdram, task->t.ucode);
    if (result != RspExitReason::Broke) wdc_unimplemented("BOSS did not finish");
    status.fetch_or(3);
    // BREAK interrupt follows the ROM's SIG1/2 classification too.
    signal((status.load() & 0x300) ? 4 : 11);
}
