// Frozen-memory regression at the real guPerspectiveF + sin/cos call path.
#undef NDEBUG
#include "src/guest_context.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
extern "C" void func_80070DB0(uint8_t*, recomp_context*);

int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input), {}};
    assert(rom.size() == 0x1000000);
    std::vector<uint8_t> frozen(8 * 1024 * 1024);
    // USA ROM text/rodata load mapping. Isolated synthetic RDRAM; no workers.
    // Word-swap the loaded image, including SDK trig constants at 800A1990.
    for (unsigned i = 0; i < 0xA2000; ++i) frozen[(0x400 + i) ^ 3] = rom[0x1000 + i];
    auto* rdram = frozen.data();
    MEM_W(0, int32_t(0x807FF010)) = std::bit_cast<uint32_t>(32.0f);
    MEM_W(0, int32_t(0x807FF014)) = std::bit_cast<uint32_t>(12288.0f);
    MEM_W(0, int32_t(0x807FF018)) = std::bit_cast<uint32_t>(1.0f);
    recomp_context source{};
    source.f_odd = &source.f0.u32h;
    source.r29 = int32_t(0x807FF000);
    source.r4 = int32_t(0x800B2A80);
    source.r5 = int32_t(0x8007ED64);
    source.r6 = std::bit_cast<uint32_t>(52.0f);
    source.r7 = std::bit_cast<uint32_t>(16.0f / 9.0f);
    std::array<uint8_t, sizeof(recomp_context)> untouched;
    std::memcpy(untouched.data(), &source, sizeof(source));
    std::array<uint32_t, 16> reference{};
    for (unsigned repeat = 0; repeat < 4; ++repeat) {
        auto memory = frozen;
        recomp_context draw;
        wdc::copy_guest_context(draw, source);
        func_80070DB0(memory.data(), &draw);
        std::array<uint32_t, 16> matrix;
        std::memcpy(matrix.data(), memory.data() + 0xB2A80, sizeof(matrix));
        if (repeat == 0) reference = matrix;
        assert(matrix == reference);
        assert(std::memcmp(&source, untouched.data(), sizeof(source)) == 0);
        assert(std::bit_cast<float>(matrix[0]) > 1.0f);
        assert(std::bit_cast<float>(matrix[5]) > 2.0f);
    }
    // Negative control: the old shallow copy must fail on this actual path.
    auto memory = frozen;
    auto broken = source;
    func_80070DB0(memory.data(), &broken);
    const bool clobbered = std::memcmp(&source, untouched.data(), sizeof(source)) != 0;
    const bool divergent = std::memcmp(reference.data(), memory.data() + 0xB2A80, sizeof(reference)) != 0;
    assert(clobbered && divergent);
    // FR=1's pointer also needs rebinding; exercise its odd-register bank.
    std::memcpy(&source, untouched.data(), sizeof(source));
    source.mips3_float_mode = 1;
    source.status_reg = 0x04000000;
    source.f_odd = &source.f1.u32l;
    recomp_context draw;
    wdc::copy_guest_context(draw, source);
    draw.f_odd[0] = 123;
    assert(draw.f1.u32l == 123 && source.f1.u32l == 0);
    std::puts("PASS: frozen projection repeats; source context intact; shallow-copy negative control diverges; FR=1 bank isolated");
}
