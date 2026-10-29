#include "boss_rsp.h"
#include "recomp.h"
#include "ultramodern/ultramodern.hpp"
#include <array>
#include <mutex>

extern "C" void osSetEventMesg_recomp(uint8_t*, recomp_context*);

// Game-side OS event registrations for the BOSS device in boss_rsp.cpp. Kept
// apart so offline replay tools link the device without the guest OS.
namespace {
struct Event { int32_t queue = 0; int32_t message = 0; };
std::array<Event, 16> events;
std::mutex event_mutex;
}

extern "C" void wdc_set_event(uint8_t* rdram, recomp_context* ctx) {
    const auto event = static_cast<unsigned>(ctx->r4);
    if (event < events.size()) {
        std::lock_guard lock(event_mutex);
        events[event] = {static_cast<int32_t>(ctx->r5), static_cast<int32_t>(ctx->r6)};
    }
    osSetEventMesg_recomp(rdram, ctx);
}

void wdc::rsp::signal_game(unsigned event) {
    Event target;
    { std::lock_guard lock(event_mutex); target = events.at(event); }
    if (target.queue) ultramodern::enqueue_external_message(target.queue, target.message, false, false);
}
