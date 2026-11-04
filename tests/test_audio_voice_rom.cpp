#undef NDEBUG
#include "recomp.h"
#include <cassert>
#include <cstdlib>
#include <vector>

extern "C" {
void func_80043A30(uint8_t*, recomp_context*);
void func_80042A44(uint8_t*, recomp_context*);
void func_80042A00(uint8_t*, recomp_context*);
void func_80042880(uint8_t*, recomp_context*);
// Zero output frames exercise the real stop/merge stage without synthesizing.
// Music sequencing and synthesis must never be reached by this fixture.
void func_80041F54(uint8_t*, recomp_context*) { std::abort(); }
void func_8004385C(uint8_t*, recomp_context*) { std::abort(); }
void func_800438D4(uint8_t*, recomp_context*) { std::abort(); }
void func_800439B0(uint8_t*, recomp_context*) { std::abort(); }
void func_80042E9C(uint8_t*, recomp_context*) { std::abort(); }
void do_break(uint32_t) { std::abort(); }
}

int main() {
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    auto context = [] { recomp_context c{}; c.r29 = int32_t(0x807FF000); return c; };
    auto merge = [&] { auto c = context(); func_80043A30(rdram, &c); };
    constexpr int channel = 6;
    constexpr int32_t voice = int32_t(0x800D47B0 + channel * 0x60);
    auto pitch = [&](uint32_t value) {
        auto c = context(); c.r4 = channel; c.r5 = value;
        func_80042A44(rdram, &c);
    };
    auto finished = [&] {
        auto c = context(); c.r4 = channel; func_80042A00(rdram, &c); return c.r2 != 0;
    };
    // Captured Hawaii whoosh: 8,240 source frames. At one synthesis block per
    // game frame, each pending pitch update used to undo the ROM's end stop.
    MEM_W(0x28, voice) = 8240;
    MEM_W(0x08, voice) = 8255;
    MEM_W(0x10, voice) = 59753;
    for (int frame = 0; frame < 4; ++frame) {
        pitch(59753);
        merge();
        assert(finished());
        assert(MEM_W(0x10, voice) == 0);
    }
    // Exact end also stops, but the final valid source frame still plays.
    for (uint32_t phase : {8239u, 8240u}) {
        MEM_W(0x08, voice) = phase;
        pitch(46836); merge();
        assert(finished() == (phase == 8240));
    }
    // Looping effects retain pitch updates past the nominal sample end.
    MEM_W(0x2C, voice) = 1;
    MEM_W(0x08, voice) = 8255;
    pitch(65536); merge();
    assert(MEM_W(0x10, voice) == 65536);
    // A new sound may reuse an ended slot: the full pending-state transfer
    // resets its phase and must not be mistaken for an old pitch update.
    MEM_W(0x2C, voice) = 0;
    MEM_W(0x84, int32_t(0x800CAFB0)) = 8240;
    auto c = context(); c.r4 = channel; c.r5 = 0; c.r6 = 0;
    func_80042880(rdram, &c);
    pitch(32768); merge();
    assert(MEM_W(0x08, voice) == 0);
    assert(MEM_W(0x10, voice) == 32768);
}
