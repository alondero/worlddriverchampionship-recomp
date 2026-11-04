#include "recomp.h"

// USA func_80043A30 before 80043B5C, game thread. The ROM stops exhausted
// one-shot voices, then merges pending pitch updates. At one synthesis block
// per rendered frame (common at 60 fps), that merge resurrects the voice on
// every frame, reading beyond the sample end. Multiple blocks at 30 fps often
// hide the problem because the second block stops it without another update.
//
// s1 identifies a ROM-owned live voice; t8 is the pending u32 16.16 pitch.
// Only the four SFX slots (channels 6..9, 0x60-byte live/pending pairs) are
// accepted. Read +08 source position, +28 source length (u32 sample frames),
// and +2C loop flags (u32, bit 0), N64 big-endian through word-swapped MEM_W.
// All addresses are fixed within 8 MiB RDRAM. No pointer survives the call;
// null memory or an unexpected slot leaves the register unchanged. The ROM
// retains all writes and volume updates. Its full-state/new-sound branch
// bypasses this instruction, so reusing a finished slot still works.
// Replace this bridge when the voice update exposes named stop/merge logic.
extern "C" void wdc_audio_oneshot_pitch(uint8_t* rdram, recomp_context* ctx) {
    constexpr uint32_t first = 0x800D47B0 + 6 * 0x60;
    constexpr uint32_t last = 0x800D47B0 + 9 * 0x60;
    const uint32_t address = uint32_t(ctx->r17);
    if (!rdram || address < first || address > last || (address - first) % 0x60) return;
    const int32_t voice = int32_t(address);
    if (!(uint32_t(MEM_W(0x2C, voice)) & 1) &&
        uint32_t(MEM_W(0x08, voice)) >= uint32_t(MEM_W(0x28, voice)))
        ctx->r24 = 0;
}
