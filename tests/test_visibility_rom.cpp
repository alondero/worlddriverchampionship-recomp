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
constexpr gpr visibility_list = int32_t(wdc::visibility::track_section_list);
int boundary_kind = 0; // car allocation, track detail, track visibility list, menu, scenery
int completed_car_lists = 0;
}
extern "C" {
void do_break(uint32_t) { std::abort(); }
recomp_func_t* get_function(int32_t) { std::abort(); }
void func_800218F0(uint8_t*, recomp_context*);
void func_800108B4(uint8_t*, recomp_context*);
void func_800230D4(uint8_t*, recomp_context*);
void func_8002343C(uint8_t*, recomp_context*);
void func_800224F0(uint8_t*, recomp_context*);
void func_80010DA0(uint8_t*, recomp_context*);
void func_8002703C(uint8_t*, recomp_context*);
void func_80025378(uint8_t*, recomp_context*);
// Inputs are already transformed into camera space. These two update helpers
// are outside the visibility decision under test.
void func_8001F7F8(uint8_t*, recomp_context*) {}
void func_8001FC64(uint8_t*, recomp_context*) {}
void func_8002D4F4(uint8_t*, recomp_context* ctx) {
    if (boundary_kind == 3) { assert(ctx->r4 == 0x307); ctx->r2 = packet; return; }
    if (boundary_kind == 4) { assert(ctx->r4 == 0x305); std::longjmp(packet_boundary, 1); }
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
void func_8000F8D8(uint8_t*, recomp_context* ctx) {
    assert(boundary_kind == 3 && ctx->r5 == packet);
    ++completed_car_lists;
}
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
UNEXPECTED(func_80019F60)
UNEXPECTED(func_80022660)
UNEXPECTED(func_8002D378)
UNEXPECTED(func_80026208)
UNEXPECTED(func_8006F63C)
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
    // Run the actual car-list loop. Rejected cars still mark their detail row;
    // a hidden preview must never enter that loop, but must finish its list.
    boundary_kind = 3;
    MEM_W(0, int32_t(0x800821B0)) = int32_t(track);
    MEM_W(0, int32_t(0x80080D90)) = 3;
    MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8004E950);
    auto menu_cars = [&](gpr active_scene, int page, bool hidden) {
        MEM_W(0, int32_t(0x8007E570)) = int32_t(active_scene);
        MEM_W(0, int32_t(0x80094810)) = page;
        put(active_scene, 0x60, 200);
        for (int i = 0; i < 3; ++i) {
            const gpr preview = int32_t(0x800BA7F0 + i * 0x18A0);
            MEM_W(0x3C4, preview) = 0;
            MEM_W(0x9B4, preview) = 0x12345678;
            put(preview, 0x624, -2); // real near rejection, before packet generation
        }
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000);
        const auto old_sp = ctx.r29;
        const int before = completed_car_lists;
        func_800108B4(rdram, &ctx);
        assert(ctx.r29 == old_sp && completed_car_lists == before + 1);
        assert(MEM_W(0, int32_t(0x80080D90)) == 3); // simulation count untouched
        for (int i = 0; i < 3; ++i)
            assert(MEM_W(0x9B4, int32_t(0x800BA7F0 + i * 0x18A0)) ==
                   (hidden ? 0x12345678 : -1));
    };
    menu_cars(int32_t(0x800ABE40), 44, true); // title
    menu_cars(int32_t(0x800ABE40), 0, true);  // main menu
    menu_cars(int32_t(0x800ABE40), 1, false); // other pages retain the ROM path
    menu_cars(int32_t(0x800ABE40), 45, false); // transitions retain the ROM path
    menu_cars(scene, 44, false); // stale menu state cannot hide race cars
    menu_cars(scene, 0, false);
    MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8000A30C);
    menu_cars(int32_t(0x800ABE40), 44, false); // races reuse the same scene storage
    menu_cars(int32_t(0x800ABE40), 0, false);
    boundary_kind = 0;
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
    // A distance of 8 must remain distance, not overlap either detail bit.
    wdc::visibility::configure(false, false, 8);
    assert(draw(100) == 7);
    wdc::visibility::configure(true, false, 8);
    assert(draw(1600) == 0);
    assert(draw(1601) == -1);
    wdc::visibility::configure(true, false, 16);
    assert(draw(12800) == 0);
    assert(draw(12801) == -1);
    wdc::visibility::configure(true, false, 4);
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
    MEM_W(0, visibility_list) = int32_t(sections);
    assert(draw(40) == 0);
    // The car consumer follows the expanded host list beyond the original
    // 64-entry guest storage.
    MEM_W(0, int32_t(0x8007E920)) = 65;
    MEM_W(0, visibility_list) = 0;
    MEM_W(64 * 4, visibility_list) = int32_t(sections);
    assert(draw(40) == 0);
    MEM_W(0x3C4, car) = 0;
    wdc::visibility::configure(false, false, 1);
    assert(draw(40) == 6);
    assert(draw(201) == -1);
    std::cout << "Enhanced: full car detail, extended distance, retained near/side rejection, live restoration\n";

    // Scenery has a separate radial range. Exercise the original squared
    // distance calculation and rejection branch up to packet allocation.
    boundary_kind = 4;
    constexpr gpr scenery = track + 0x800, position = scenery + 0x40;
    MEM_W(0x10, scenery) = int32_t(position);
    auto submit_scenery = [&](float x, float y) {
        put(position, 0, x);
        put(position, 4, y);
        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000);
        ctx.r4 = scenery;
        if (setjmp(packet_boundary) != 0) return true;
        func_800224F0(rdram, &ctx);
        return false;
    };
    assert(submit_scenery(383, 0));
    assert(!submit_scenery(384, 0));
    assert(!submit_scenery(300, 300));
    assert(!submit_scenery(512, 0));
    wdc::visibility::configure(false, false, 2);
    assert(submit_scenery(512, 0));
    assert(submit_scenery(767, 0));
    assert(!submit_scenery(768, 0));
    wdc::visibility::configure(false, false, 8);
    assert(submit_scenery(512, 0));
    assert(submit_scenery(3071, 0));
    assert(!submit_scenery(3072, 0));
    wdc::visibility::configure(false, false, 16);
    assert(submit_scenery(3072, 0));
    assert(submit_scenery(24575, 0));
    assert(!submit_scenery(24576, 0));
    wdc::visibility::configure(false, true, 1);
    assert(!submit_scenery(512, 0));
    wdc::visibility::configure(false, false, 1);

    // Run the ROM's sort-table setup and insert projected track triangles
    // through its real depth rejection and packet writer.
    constexpr gpr commands = int32_t(0x80500010), primary_top = int32_t(0x80520000);
    constexpr gpr secondary_top = int32_t(0x80540000);
    constexpr gpr render_mesh = int32_t(0x80550000), mesh_ref = render_mesh + 0x40;
    constexpr gpr mesh = mesh_ref + 0x40, polygon = mesh + 0x40;
    constexpr gpr vertices = polygon + 0x40, material = vertices + 0x40;
    MEM_W(0, render_mesh) = int32_t(mesh_ref);
    MEM_W(4, render_mesh) = int32_t(vertices);
    MEM_W(0, mesh_ref) = int32_t(mesh);
    MEM_W(8, mesh) = int32_t(polygon);
    MEM_W(0xC, mesh) = 1;
    MEM_W(0x10, mesh) = MEM_W(0x18, mesh) = int32_t(material);
    MEM_W(0, material) = MEM_W(4, material) = MEM_W(8, material) = int32_t(material + 0x40);
    MEM_H(0, polygon) = -64; // authored sort bias
    MEM_H(0xA, polygon) = 1;
    MEM_H(0xC, polygon) = 2;
    MEM_H(0xE, polygon) = -1; // triangle
    MEM_H(2, vertices + 16) = 200;
    MEM_H(0, vertices + 32) = 200;
    MEM_W(0, int32_t(0x8007EF48)) = 0;
    MEM_W(0, int32_t(0x8007EF4C)) = 0;
    MEM_W(0, int32_t(0x8007EF58)) = 0;
    put(scene, 0x60, 384);

    auto sort_triangle = [&](int scale, bool race, int depth, int buckets, bool expected) {
        wdc::visibility::configure(false, false, scale);
        MEM_W(0, int32_t(0x8007D848)) = int32_t(race ? 0x8000C560 : 0x8004E950);
        MEM_W(0, int32_t(0x8007ED40)) = int32_t(commands - 8);
        MEM_W(0, int32_t(0x8007ED84)) = int32_t(commands - 8);
        MEM_W(0, int32_t(0x8007FE70)) = int32_t(primary_top);
        MEM_W(0, int32_t(0x8007FE74)) = int32_t(secondary_top);
        for (int i = 0; i < 3; ++i) MEM_H(0xE, vertices + 16 * i) = depth * 32;

        recomp_context ctx{};
        ctx.f_odd = &ctx.f0.u32h;
        ctx.r29 = int32_t(0x807FF000);
        ctx.r4 = scene;
        func_80010DA0(rdram, &ctx);
        const gpr tails = MEM_W(0, int32_t(0x8007ED9C));
        assert(MEM_W(0, int32_t(0x8007EDA0)) == buckets);
        assert(MEM_W(0, int32_t(0x8007ED98)) == int32_t(commands));
        assert(MEM_W(0, int32_t(0x8007ED84)) == int32_t(commands + 4 * buckets));
        const bool extended = buckets > 1152;
        assert(tails == (extended ? int32_t(0x807C0000) : primary_top - 4 * buckets));
        assert(MEM_W(0, int32_t(0x8007FE70)) == int32_t(extended ? primary_top : tails));
        assert(MEM_W(4 * (buckets - 1), tails) == int32_t(commands + 4 * (buckets - 1)));

        ctx.r4 = render_mesh;
        ctx.r5 = tails;
        ctx.r6 = buckets - 1;
        ctx.r7 = material;
        func_8002703C(rdram, &ctx);
        assert(MEM_W(0, int32_t(0x8007FE74)) == int32_t(secondary_top - (expected ? 0x40 : 0)));
        if (expected) {
            const int index = buckets - 1 - (3 * depth + 64);
            assert(MEM_W(4 * index, tails) == int32_t(secondary_top - 0x40));
        }
    };
    sort_triangle(1, true, 350, 1152, true);
    sort_triangle(1, true, 410, 1152, false);
    sort_triangle(2, true, 410, 2304, true);
    sort_triangle(8, true, 960, 4096, true);
    sort_triangle(16, true, 960, 4096, true);
    sort_triangle(16, false, 410, 1152, false);
    sort_triangle(1, true, 410, 1152, false);

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
    // Real linked-list producer, relocated to host-reserved extended memory.
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
        assert(count > 0 && count <= 1024);
        for (int i = 0; i < count; ++i) for (int j = i + 1; j < count; ++j)
            assert(MEM_W(i * 4, visibility_list) != MEM_W(j * 4, visibility_list));
        return count;
    };
    ring(256, 8, 8);
    assert(track_list(1) == 17);
    assert(track_list(2) == 33);
    assert(track_list(4) == 65);
    assert(track_list(8) == 129);
    assert(track_list(16) == 256);
    ring(1100, 8, 8);
    assert(track_list(16) == 1024); // bounded safety cap, no write past the reservation
    ring(256, 8, 8);
    assert(track_list(1) == 17);
    ring(32, 0, 8);
    assert(track_list(1) == 9); // byte-sized authored counts keep independent directions
    ring(32, 8, 0);
    assert(track_list(1) == 9);
    ring(20, 8, 8);
    assert(track_list(16) == 20); // wrap without repeated sections
    ring(20, 0, 0);
    assert(track_list(16) == 20); // Unlimited follows valid links even when authored counts are zero
    ring(256, 8, 8);
    MEM_W(0, sections + 8 * 0x40) = 0; // invalid extension, original list valid
    assert(track_list(2) == 25);
    // Distance 8 alone must not enable the full track detail bit.
    boundary_kind = 1;
    wdc::visibility::configure(false, false, 8);
    assert(track_detail(288) == 0x4000);

    // Run the original arena initializer and descending allocator using the
    // New Zealand race's heap bounds. Only the primary reservation may move.
    auto check_arena = [&](int scale, uint32_t size, bool expanded) {
        constexpr int32_t heap = int32_t(0x800E3880);
        constexpr uint32_t top = 0x2EDAE0;
        const uint32_t primary_top = top - 0x8000 - 2 * 0x3C000;
        MEM_W(0, int32_t(0x800924A0)) = heap;
        MEM_W(0, int32_t(0x800924A8)) = 0x4B000;
        MEM_W(0, int32_t(0x800924AC)) = top;
        wdc::visibility::configure(false, false, scale);
        recomp_context ctx{};
        ctx.r29 = int32_t(0x807FF000);
        ctx.r4 = size; ctx.r5 = 0x3C000; ctx.r6 = 0x8000;
        func_80025378(rdram, &ctx);
        assert(ctx.r29 == int32_t(0x807FF000));
        assert(MEM_W(0, int32_t(0x800B3838)) == ADD32(heap, top - 0x8000));
        assert(MEM_W(0, int32_t(0x800B3834)) == ADD32(heap, primary_top + 0x3C000));
        assert(MEM_W(0, int32_t(0x800B3830)) == ADD32(heap, primary_top));
        assert(uint32_t(MEM_W(0, int32_t(0x8007ED50))) ==
               (expanded ? 0x80700000u : uint32_t(heap) + primary_top - size));
        assert(uint32_t(MEM_W(0, int32_t(0x8007ED54))) * 8 == (expanded ? size * 2 : size));
        assert(uint32_t(MEM_W(0, int32_t(0x800924AC))) == (expanded ? primary_top : primary_top - size));
        assert(MEM_W(0, int32_t(0x800924A8)) == 0x4B000);
    };
    check_arena(1, 0x40000, false);
    check_arena(2, 0x40000, true);
    check_arena(8, 0x60000, true);
    check_arena(16, 0x40000, true);
    check_arena(16, 0x38000, false); // menu allocation remains unchanged
    std::cout << "Track: original/detail override, mode remaps, expanded list relocation, wrap and invalid extension\n";
}
