#include "recomp.h"

extern "C" void func_8000A30C(uint8_t*, recomp_context*);

// USA 8000C870, game thread. The dispatcher owns the live context and RDRAM
// for the duration of the call. Its original draw owns audio, arena turnover
// and graphics submission. All guest outputs flow back to that same caller.
extern "C" void wdc_render_race_draw(uint8_t* rdram, recomp_context* ctx) {
    func_8000A30C(rdram, ctx);
}
