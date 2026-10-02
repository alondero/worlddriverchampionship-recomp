#include <cstdio>
#include <cstdlib>
#include "recomp.h"
#include "guest_memory.h"
#include "librecomp/game.hpp"

extern "C" [[noreturn]] void wdc_unimplemented(const char* name) {
    std::fprintf(stderr, "[wdc] unsupported guest operation: %s\n", name);
    std::fflush(nullptr);
    std::_Exit(3);
}


// 8002D7F0 only aligns a range and invalidates D-cache lines through a computed
// jump into an unrolled cache loop. Host RDRAM is coherent; no memory writes.
extern "C" void wdc_invalidate_cache(uint8_t*, recomp_context*) {}


// osPiReadIo's a0 is a cartridge byte offset; a1 is a guest u32 destination.
// Runs on the calling guest thread; bounds are checked here, byte swapping is
// performed by the runtime. Invalid requests return -1 without a memory write.
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t offset = static_cast<uint32_t>(ctx->r4) & 0x0FFFFFFFu;
    const uint32_t dest = static_cast<uint32_t>(ctx->r5) & 0x1FFFFFFFu;
    if (recomp::get_rom().size() < 4 || offset > recomp::get_rom().size() - 4 ||
        static_cast<uint32_t>(ctx->r5) < 0x80000000u ||
        static_cast<uint32_t>(ctx->r5) >= 0x80800000u ||
        dest > 0x800000 - 4 || (offset | dest) & 3) {
        ctx->r2 = -1;
        return;
    }
    recomp::do_rom_pio(rdram, ctx->r5, offset | 0x10000000u);
    ctx->r2 = 0;
}
