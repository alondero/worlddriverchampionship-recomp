#undef NDEBUG
#include "recomp.h"
#include "game_visibility.h"
#include <bit>
#include <cassert>
#include <csetjmp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::jmp_buf packet_boundary;
constexpr gpr car = int32_t(0x80400000), scene = int32_t(0x80402000);
constexpr gpr camera = int32_t(0x80403000), track = int32_t(0x80404000);
constexpr gpr sections = int32_t(0x80405000), packet = int32_t(0x80410000);
int boundary_kind = 0; // car allocation, track detail, track visibility list
}
extern "C" {
void do_break(uint32_t) { std::abort(); }
void func_800218F0(uint8_t*, recomp_context*);
void func_800230D4(uint8_t*, recomp_context*);
void func_8002343C(uint8_t*, recomp_context*);
// Inputs are already transformed into camera space. These two update helpers
// are outside the visibility decision under test.
void func_8001F7F8(uint8_t*, recomp_context*) {}
void func_8001FC64(uint8_t*, recomp_context*) {}
void func_8002D4F4(uint8_t*, recomp_context* ctx) {
    if (boundary_kind == 0) std::longjmp(packet_boundary, 1);
    assert(boundary_kind == 1 && ctx->r4 == 0x301);
    ctx->r2 = packet;
}
void func_8002D2B0(uint8_t*, recomp_context* ctx) { assert(boundary_kind == 1); ctx->r2 = packet + 0x100; }
void func_8002D1FC(uint8_t*, recomp_context* ctx) { assert(boundary_kind == 1); ctx->r2 = packet + 0x200; }
void func_8002CFB4(uint8_t*, recomp_context*) { assert(boundary_kind == 1); std::longjmp(packet_boundary, 1); }
void func_80022D84(uint8_t*, recomp_context*) { assert(boundary_kind == 2); std::longjmp(packet_boundary, 1); }
// Track-list matrix setup is outside list selection. No list writes are mocked.
void func_80071580(uint8_t*, recomp_context*) { assert(boundary_kind == 2); }
void func_8001A0C0(uint8_t*, recomp_context*) { assert(boundary_kind == 2); }
void func_8001A8E8(uint8_t*, recomp_context*) { assert(boundary_kind == 2); }
void func_8002CF30(uint8_t*, recomp_context* ctx) { assert(boundary_kind == 2); ctx->r2 = packet; }
// Any unexpected path fails instead of silently mocking another decision.
#define UNEXPECTED(name) void name(uint8_t*, recomp_context*) { std::abort(); }
UNEXPECTED(func_80001D4C)
UNEXPECTED(func_8000F8D8)
UNEXPECTED(func_800153F4)
UNEXPECTED(func_8001A040)
UNEXPECTED(func_8001A79C)
UNEXPECTED(func_8001A97C)
UNEXPECTED(func_80023BC0)
UNEXPECTED(func_80023CFC)
UNEXPECTED(func_80023F2C)
UNEXPECTED(func_8002D314)
UNEXPECTED(func_8003062C)
UNEXPECTED(func_8001FBB0)
UNEXPECTED(func_80022E34)
UNEXPECTED(func_8001A9BC)
}
int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    assert(argc == 2);
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    std::ifstream rom(argv[1], std::ios::binary);
    assert(rom);
    rom.seekg(0x1000);
    std::vector<char> data(0x100000);
    rom.read(data.data(), data.size());
    assert(rom);
    for (size_t i = 0; i < data.size(); ++i) MEM_B(i, int32_t(0x80000400)) = data[i];
    auto put = [&](gpr address, int offset, float value) {
        MEM_W(offset, address) = std::bit_cast<uint32_t>(value);
    };
    MEM_W(0, int32_t(0x800821B0)) = int32_t(track);
    MEM_W(0, int32_t(0x8007E570)) = int32_t(scene);
    MEM_W(0, int32_t(0x8007ED6C)) = int32_t(camera);
    MEM_W(0, int32_t(0x8007D808)) = 1; // car rendering enabled
    MEM_W(0, int32_t(0x8007F748)) = 1; // LOD stepping enabled
    MEM_W(0, int32_t(0x800923E8)) = 0; // no debug size multiplier
    put(scene, 0x60, 200); put(scene, 0x98, 100);
    put(camera, 0x7C, 1);
    auto draw = [&](float depth, float lateral = 0) {
        put(car, 0x624, depth); put(car, 0x628, lateral);
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000); ctx.r4 = car;
        if (setjmp(packet_boundary) == 0) {
            func_800218F0(rdram, &ctx);
            return -1;
        }
        return int(MEM_W(0x9B4, car));
    };
    assert(draw(2) == 0);
    assert(draw(10) == 3);
    assert(draw(40) == 6);
    assert(draw(100) == 7);
    assert(draw(125) == 7);
    assert(draw(126) == -1); // final detail row hides the car before the far plane
    assert(draw(201) == -1);
    assert(draw(-2) == -1);
    assert(draw(10, 16) == -1);
    assert(draw(10, -16) == -1);
    std::cout << "ROM baseline: distance selects detail rows 0/3/6/7, then rejects far cars\n";
    wdc::visibility::configure(true, false, 1);
    for (float depth : {2.f, 10.f, 40.f, 100.f, 200.f}) assert(draw(depth) == 0);
    assert(draw(201) == -1); // detail does not bypass distance
    wdc::visibility::configure(true, false, 4);
    assert(draw(201) == 0);
    assert(draw(800) == 0);
    assert(draw(801) == -1);
    assert(draw(-2) == -1);
    assert(draw(10, 16) == -1);
    assert(draw(10, -16) == -1);
    // Preserve actual projected size for fading, not the comparison override.
    assert(draw(100) == 0);
    assert(std::bit_cast<float>(uint32_t(MEM_W(0x9B0, car))) == 2.5f);
    // Section visibility is independent of detail: distant cars must belong
    // to a submitted track section, while near cars bypass this membership test.
    MEM_W(0x3C4, car) = int32_t(packet);
    MEM_W(0x34, track) = int32_t(track + 0x400);
    MEM_W(0x400, track) = int32_t(sections);
    MEM_W(0, int32_t(0x8007E920)) = 0;
    assert(draw(40) == -1);
    assert(draw(10) == 0);
    MEM_W(0, int32_t(0x8007E920)) = 1;
    MEM_W(0, int32_t(0x800B2880)) = int32_t(sections);
    assert(draw(40) == 0);
    MEM_W(0x3C4, car) = 0;
    wdc::visibility::configure(false, false, 1);
    assert(draw(40) == 6);
    assert(draw(201) == -1);
    std::cout << "Enhanced: full car detail, extended distance, retained near/side rejection, live restoration\n";

    // Actual track depth->detail bucket and mode-remapping routines.
    boundary_kind = 1;
    put(camera, 0, 1);
    auto track_detail = [&](int depth) {
        MEM_W(0xC, sections) = depth * 65536; // 16.16 world position
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000); ctx.r4 = sections;
        if (setjmp(packet_boundary) == 0) {
            func_800230D4(rdram, &ctx);
            std::abort();
        }
        return uint32_t(MEM_W(0x18, packet));
    };
    MEM_W(0, int32_t(0x8007E580)) = 0;
    assert(track_detail(47) == 0x100);
    assert(track_detail(48) == 0x200);
    assert(track_detail(288) == 0x4000);
    for (int mode : {0, 2}) for (int secondary : {0, 1}) {
        MEM_W(0, int32_t(0x8007E580)) = mode;
        MEM_W(0x7C, scene) = secondary;
        wdc::visibility::configure(false, true, 1);
        for (int depth : {0, 47, 48, 96, 144, 192, 240, 288, 500})
            assert(track_detail(depth) == 0x100);
    }
    wdc::visibility::configure(false, false, 1);
    MEM_W(0, int32_t(0x8007E580)) = 0;
    MEM_W(0x7C, scene) = 0;
    assert(track_detail(288) == 0x4000);

    // Real linked-list producer: 64 slots followed immediately by matrix data.
    boundary_kind = 2;
    MEM_W(0, int32_t(0x8007DF70)) = 1;
    MEM_W(0, int32_t(0x8007E574)) = 0;
    MEM_W(0x2E0, scene) = int32_t(car);
    MEM_W(0x34, track) = int32_t(track + 0x400);
    MEM_W(0x400, track) = int32_t(sections);
    auto ring = [&](int size, int ahead, int behind) {
        for (int i = 0; i < size; ++i) {
            const gpr node = sections + i * 0x40;
            MEM_W(0, node) = int32_t(sections + (i + 1) % size * 0x40);
            MEM_W(4, node) = int32_t(sections + (i + size - 1) % size * 0x40);
            MEM_B(0x1F, node) = 0; // no scenery-update work
        }
        MEM_B(0x1C, sections) = ahead;
        MEM_B(0x1D, sections) = behind;
    };
    auto track_list = [&](int scale) {
        wdc::visibility::configure(false, false, scale);
        MEM_W(0, int32_t(0x800B2980)) = 0x12345678;
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000);
        if (setjmp(packet_boundary) == 0) { func_8002343C(rdram, &ctx); std::abort(); }
        assert(MEM_W(0, int32_t(0x800B2980)) == 0x12345678);
        const int count = MEM_W(0, int32_t(0x8007E920));
        assert(count > 0 && count <= 64);
        for (int i = 0; i < count; ++i) for (int j = i + 1; j < count; ++j)
            assert(MEM_W(i * 4, int32_t(0x800B2880)) != MEM_W(j * 4, int32_t(0x800B2880)));
        return count;
    };
    ring(128, 8, 8);
    assert(track_list(1) == 17);
    assert(track_list(2) == 33);
    assert(track_list(4) == 64);
    assert(track_list(1) == 17);
    ring(20, 8, 8);
    assert(track_list(4) == 20); // wrap without repeated sections
    ring(128, 8, 8);
    MEM_W(0, sections + 8 * 0x40) = 0; // invalid extension, original list valid
    assert(track_list(2) == 25);
    std::cout << "Track: original/detail override, mode remaps, bounded list expansion, wrap and invalid extension\n";
}
