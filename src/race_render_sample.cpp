#include "race_render_sample.h"
#include "guest_context.h"

extern "C" void func_800252D0(uint8_t*, recomp_context*);

namespace wdc::render {
RenderBank::Transfer capture_race_sample(uint8_t* rdram, const recomp_context& caller,
                                         bool prefer_write_watch, void (*wait_renderer)(uint8_t*)) {
    // USA 800252D0 submits an empty next transform batch, consumes SP/DP
    // completion via the normal swap path, and clears 8007EDB8. It can
    // yield to guest workers, so acquire the bank only after the host fence.
    recomp_context drain_context;
    wdc::copy_guest_context(drain_context, caller);
    func_800252D0(rdram, &drain_context);
    wait_renderer(rdram);
    auto& bank = race_render_bank();
    std::lock_guard lock(bank.mutex);
    bank.initialize(prefer_write_watch);
    return bank.capture(rdram);
}
}
