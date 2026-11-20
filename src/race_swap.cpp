#include "race_swap.h"
#include "recomp.h"

extern "C" void osRecvMesg_recomp(uint8_t*, recomp_context*);
extern "C" void osViSwapBuffer_recomp(uint8_t*, recomp_context*);

namespace wdc::render {
static thread_local const RaceSwap* active = nullptr;
RaceSwap::RaceSwap(const uint8_t* image, void (*callback)())
    : before_present(callback), image_(image), previous_(active) { active = this; }
RaceSwap::~RaceSwap() { active = previous_; }
const RaceSwap* RaceSwap::for_image(const uint8_t* image) {
    return active && active->image_ == image ? active : nullptr;
}
}

// USA func_800013DC, after the original delay-slot argument setup. The six
// VI waits take the live 24-byte queue's KSEG0 address, a null destination
// and OS_MESG_BLOCK. A selected draw can complete this wait without consuming
// a retrace. All other queue operations still follow the SDK.
extern "C" void wdc_render_swap_wait(uint8_t* rdram, recomp_context* ctx) {
    if (wdc::render::RaceSwap::for_image(rdram) && uint32_t(ctx->r4) == 0x800AAA28 &&
        ctx->r5 == 0 && ctx->r6 == 1) {
        ctx->r2 = 0;
        return;
    }
    osRecvMesg_recomp(rdram, ctx);
}

// The ROM loads a0 from the aligned guest framebuffer word at 8007D750.
// The SDK retains ownership of the VI latch and queued presentation.
extern "C" void wdc_render_swap_buffer(uint8_t* rdram, recomp_context* ctx) {
    if (const auto* draw = wdc::render::RaceSwap::for_image(rdram)) {
        if (!draw->before_present) return;
        draw->before_present();
    }
    osViSwapBuffer_recomp(rdram, ctx);
}
