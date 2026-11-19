#undef NDEBUG
#include "game_camera.h"
#include "game_projection.h"
#include "game_visibility.h"
#include "guest_memory.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

int test_projection_runtime(const char* rom_path, const char* mods_path,
                            std::span<const uint8_t> initial, const recomp_context& input);

extern "C" {
void projection_reference_stock(uint8_t*, recomp_context*);
void projection_reference_enhanced(uint8_t*, recomp_context*);
void func_800006E0(uint8_t*, recomp_context*);
void func_8000FC18(uint8_t*, recomp_context*);

// Layout/descriptor setup only. The five projection callees and all their
// reachable bodies, including the matrix allocator, execute in both lanes.
void func_8003C9A4(uint8_t*, recomp_context*) {}
void func_8003C95C(uint8_t*, recomp_context* ctx) { ctx->r2 = int32_t(0x80500000); }
void func_8000E020(uint8_t*, recomp_context*) {}
void func_8000E18C(uint8_t*, recomp_context*) {}
void func_8000E1BC(uint8_t*, recomp_context*) {}
void func_8000F988(uint8_t*, recomp_context*) {}
void func_80012024(uint8_t*, recomp_context*) {}
void func_80012218(uint8_t*, recomp_context*) {}
void func_800122F4(uint8_t*, recomp_context*) {}
void func_8002A334(uint8_t*, recomp_context*) {}
}

namespace {
constexpr size_t ram_size = 8 * 1024 * 1024;
constexpr size_t guard_size = 64;
constexpr gpr camera = int32_t(0x80400000);
constexpr gpr stack = int32_t(0x807FF000);
constexpr gpr descriptor_table = int32_t(0x800AAE38);
constexpr gpr active_display = int32_t(0x8007D75C);
constexpr gpr view = int32_t(0x800B2AC0);
constexpr gpr projection = int32_t(0x800B2A80);
constexpr gpr combined = int32_t(0x800B2A40);
constexpr gpr normalization = int32_t(0x8007ED64);
constexpr gpr published = int32_t(0x8007ED68);
constexpr gpr alloc_limit = int32_t(0x8007ED40);
constexpr gpr alloc_cursor = int32_t(0x8007FE70);
constexpr gpr alloc_count = int32_t(0x8007FE78);
constexpr gpr packet_end = int32_t(0x80403000);
constexpr gpr packet = packet_end - 0x40;
constexpr gpr scene = int32_t(0x800ABE40);
constexpr gpr second_scene = scene + 0x2830;
constexpr auto enhanced = wdc::camera::ProjectionPolicy::Enhanced;
constexpr auto stock = wdc::camera::ProjectionPolicy::Stock;

float reference_width_scale = 1;
float reference_distance_scale = 1;

struct Bank {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(ram_size + 2 * guard_size, 0x6D);
    uint8_t* ram() { return bytes.data() + guard_size; }
};

size_t index(gpr address) { return uint32_t(address) - 0x80000000u; }

void write_float(uint8_t* rdram, gpr address, int offset, float value) {
    MEM_W(offset, address) = std::bit_cast<int32_t>(value);
}

void copy_context(recomp_context& dest, const recomp_context& source) {
    dest = source;
    dest.f_odd = &dest.f0.u32h;
}

struct Call {
    unsigned callee;
    std::array<gpr, 4> arguments;
    std::vector<uint8_t> input;
    std::vector<uint8_t> output;
    gpr result = 0;
    bool operator==(const Call&) const = default;
};

using Trace = std::vector<Call>;
Trace* active_trace;

void append(std::vector<uint8_t>& bytes, const uint8_t* rdram, gpr address, size_t size) {
    bytes.insert(bytes.end(), rdram + index(address), rdram + index(address) + size);
}

void read_allocator(std::vector<uint8_t>& bytes, const uint8_t* rdram) {
    append(bytes, rdram, alloc_limit, 4);
    append(bytes, rdram, alloc_cursor, 4);
    append(bytes, rdram, alloc_count, 4);
    append(bytes, rdram, packet, 0x40);
}

void snapshot_input(Call& call, const uint8_t* rdram, const recomp_context& ctx) {
    switch (call.callee) {
        case 0: append(call.input, rdram, camera, 0x2C); break;
        case 1:
            append(call.input, rdram, ctx.r29 + 0x10, 12);
            append(call.input, rdram, projection, 0x40);
            append(call.input, rdram, normalization, 4);
            break;
        case 2:
            append(call.input, rdram, view, 0x40);
            append(call.input, rdram, normalization, 4);
            break;
        case 3:
            append(call.input, rdram, view, 0x40);
            append(call.input, rdram, projection, 0x40);
            break;
        case 4:
            append(call.input, rdram, combined, 0x40);
            read_allocator(call.input, rdram);
            break;
    }
}

void snapshot_output(Call& call, const uint8_t* rdram, const recomp_context& ctx) {
    switch (call.callee) {
        case 0: case 2: append(call.output, rdram, view, 0x40); break;
        case 1:
            append(call.output, rdram, projection, 0x40);
            append(call.output, rdram, normalization, 4);
            break;
        case 3: append(call.output, rdram, combined, 0x40); break;
        case 4:
            read_allocator(call.output, rdram);
            call.result = ctx.r2;
            break;
    }
}

// Original 80011D54's saved ra and four incoming-argument spills. The
// recovered body retains the o32 outgoing slots and the helpers' frame depth.
// The original caller reloads its own scene pointer after return, and the
// five callees neither read these five spill words nor publish their addresses.
constexpr std::array dead_spills{stack - 4, stack, stack + 4, stack + 8, stack + 12};

size_t ram_difference(const Bank& first, const Bank& second) {
    size_t start = 0;
    for (gpr address : dead_spills) {
        const size_t end = guard_size + index(address);
        const auto result = std::mismatch(first.bytes.data() + start, first.bytes.data() + end,
                                          second.bytes.data() + start);
        if (result.first != first.bytes.data() + end)
            return result.first - first.bytes.data();
        start = end + 4;
    }
    return std::mismatch(first.bytes.data() + start, first.bytes.data() + first.bytes.size(),
                         second.bytes.data() + start).first - first.bytes.data();
}

void check_writes(const Bank& initial, const Bank& result) {
    // Includes the deepest helper spill below the projection frame and its
    // incoming argument words above it. Differential comparison still checks
    // the individual bytes inside this window except for the five dead spills.
    constexpr std::array ranges{
        std::pair{normalization + 2, normalization + 4}, std::pair{published, published + 4},
        std::pair{alloc_cursor, alloc_cursor + 4},
        std::pair{alloc_count, alloc_count + 4}, std::pair{combined, view + 0x40},
        std::pair{camera + 0x7C, camera + 0x80}, std::pair{packet, packet_end},
        std::pair{stack - 0x100, stack + 0x10},
    };
    size_t start = 0;
    for (const auto [lo, hi] : ranges) {
        const size_t end = guard_size + index(lo);
        assert(std::equal(initial.bytes.data() + start, initial.bytes.data() + end, result.bytes.data() + start));
        start = guard_size + index(hi);
    }
    assert(std::equal(initial.bytes.data() + start, initial.bytes.data() + initial.bytes.size(),
                      result.bytes.data() + start));
}

void check_context(const recomp_context& reference, const recomp_context& candidate) {
    assert(reference.f_odd == &reference.f0.u32h && candidate.f_odd == &candidate.f0.u32h);
    assert(reference.status_reg == candidate.status_reg && reference.mips3_float_mode == candidate.mips3_float_mode);
    const auto ref = std::as_bytes(std::span{&reference, 1});
    const auto got = std::as_bytes(std::span{&candidate, 1});
    const auto end = ref.begin() + offsetof(recomp_context, f_odd);
    const auto diff = std::mismatch(ref.begin(), end, got.begin());
    if (diff.first != end) {
        std::cerr << "Register context differs at byte " << diff.first - ref.begin() << '\n';
        std::abort();
    }
}

void negative_controls(Bank& candidate, const Bank& reference, const Trace& actual, const Trace& expected) {
    auto flip_and_detect = [&](gpr address) {
        const auto offset = guard_size + index(address);
        candidate.bytes[offset] ^= 1;
        assert(ram_difference(reference, candidate) == offset);
        candidate.bytes[offset] ^= 1;
    };
    flip_and_detect(projection);
    flip_and_detect(normalization + 2); // u16 in a word-swapped bank
    flip_and_detect(packet - 1);        // outside the allocator's output
    flip_and_detect(stack - 8);         // unmasked stack word
    auto reordered = actual;
    std::swap(reordered[0], reordered[1]);
    assert(reordered != expected);
}

void compare(const Bank& initial, Bank& reference, Bank& candidate, const recomp_context& input,
             wdc::camera::ProjectionPolicy policy, bool controls = false) {
    reference.bytes = initial.bytes;
    candidate.bytes = initial.bytes;
    recomp_context ref{}, got{};
    copy_context(ref, input);
    copy_context(got, input);
    // Verify odd-half rebinding by writing the live slot in each independent bank.
    *ref.f_odd = 0x5A5A5A5A;
    assert(*got.f_odd == input.f0.u32h);
    *got.f_odd = *ref.f_odd;

    Trace expected, actual;
    active_trace = &expected;
    auto* entry = policy == stock ? projection_reference_stock : projection_reference_enhanced;
    entry(reference.ram(), &ref);
    active_trace = &actual;
    if (policy == stock)
        wdc::camera::setup_projection(candidate.ram(), &got, stock);
    else
        func_80011D54(candidate.ram(), &got);
    active_trace = nullptr;
    assert(expected.size() == 5);
    for (unsigned callee = 0; callee < expected.size(); ++callee) assert(expected[callee].callee == callee);
    if (const auto diff = std::mismatch(expected.begin(), expected.end(), actual.begin(), actual.end());
        diff.first != expected.end() || diff.second != actual.end()) {
        std::cerr << "Call trace differs at call " << diff.first - expected.begin() << '\n';
        std::abort();
    }
    check_context(ref, got);

    const size_t difference = ram_difference(reference, candidate);
    if (difference != candidate.bytes.size()) {
        std::cerr << "Persistent RAM differs at guest byte " << std::hex << 0x80000000 + difference - guard_size << '\n';
        std::abort();
    }
    check_writes(initial, reference);
    check_writes(initial, candidate);
    auto* rdram = candidate.ram();
    assert(got.r2 == packet && got.r29 == input.r29 && got.r31 == input.r31);
    assert(MEM_W(0, published) == int32_t(packet) && MEM_W(0, alloc_cursor) == int32_t(packet));
    assert(MEM_W(0, alloc_count) == 8);
    assert(!std::equal(initial.bytes.data() + guard_size + index(packet),
                       initial.bytes.data() + guard_size + index(packet_end), rdram + index(packet)));
    if (controls) negative_controls(candidate, reference, actual, expected);
}
}

extern "C" void projection_test_call(uint8_t* rdram, recomp_context* ctx, unsigned callee, recomp_func_t* body) {
    if (!active_trace) {
        body(rdram, ctx);
        return;
    }
    constexpr std::array argument_count{2, 4, 2, 3, 1};
    Call call{callee, {ctx->r4, ctx->r5, ctx->r6, ctx->r7}, {}, {}};
    std::fill(call.arguments.begin() + argument_count.at(callee), call.arguments.end(), 0);
    snapshot_input(call, rdram, *ctx);
    body(rdram, ctx);
    snapshot_output(call, rdram, *ctx);
    active_trace->push_back(std::move(call));
}

// Frozen legacy enhancements run only in the independently generated lane.
extern "C" void projection_reference_aspect(uint8_t* rdram, recomp_context* ctx) {
    wdc_require_guest_range(ctx->r14, 8, 4);
    const int32_t width = MEM_W(0, ctx->r14);
    const int32_t height = MEM_W(4, ctx->r14);
    if (width <= 0 || width > 4096 || height <= 0 || height > 4096) {
        ctx->f0.fl *= reference_width_scale;
        return;
    }
    ctx->f0.fl = ctx->f8.fl * (float(height) / float(width)) * (4.0f / 3.0f) * reference_width_scale;
}

extern "C" void projection_reference_distance(uint8_t*, recomp_context* ctx) {
    ctx->f10.fl *= reference_distance_scale;
}

extern "C" void projection_reference_normalize(uint8_t* rdram, recomp_context* ctx) {
    const float distance = std::bit_cast<float>(uint32_t(MEM_W(0x34, ctx->r29)));
    if (!std::isfinite(distance) || distance < 0) return;
    const float sum = 32.0f + distance * 32.0f;
    const auto norm = uint16_t(std::max(1.0, std::min(65535.0, 131072.0 / double(sum))));
    MEM_H(0, normalization) = norm;
}

int main(int argc, char** argv) {
    assert(argc == 2 || (argc == 4 && (std::string_view(argv[2]) == "--invalid-descriptor" ||
                                     std::string_view(argv[2]) == "--runtime-mods")));
    Bank initial, reference, candidate;
    auto* rdram = initial.ram();
    std::fill_n(rdram, ram_size, 0);
    std::ifstream rom(argv[1], std::ios::binary);
    rom.seekg(0x1000);
    std::vector<char> data(0x100000);
    rom.read(data.data(), data.size());
    assert(rom);
    for (size_t i = 0; i < data.size(); ++i) MEM_B(i, int32_t(0x80000400)) = data[i];

    MEM_W(0, int32_t(0x80000300)) = 1; // NTSC
    recomp_context init{};
    init.f_odd = &init.f0.u32h;
    init.r29 = stack;
    func_800006E0(rdram, &init);

    // A nontrivial camera orientation makes matrix order and packet placement
    // observable. These are finite values in the original camera field units.
    write_float(rdram, camera, 0x00, 0.125f);
    write_float(rdram, camera, 0x08, -0.75f);
    write_float(rdram, camera, 0x10, -0.75f);
    write_float(rdram, camera, 0x14, 0.25f);
    write_float(rdram, camera, 0x24, 0.875f);
    write_float(rdram, camera, 0x68, 1.04719755f);
    write_float(rdram, camera, 0x74, std::tan(1.04719755f));
    MEM_W(0, alloc_cursor) = int32_t(packet_end);
    MEM_W(0, alloc_limit) = int32_t(packet - 0x100);
    MEM_W(0, alloc_count) = 7;
    std::fill_n(rdram + index(packet), 0x40, 0xC5);
    std::fill_n(rdram + index(combined), 0xC0, 0xB7);

    recomp_context input{};
    input.f_odd = &input.f0.u32h;
    input.r4 = camera;
    input.r29 = stack;
    input.r31 = int32_t(0x80010AF4);
    input.r1 = 0x10203040;
    input.r8 = 0x23456789;
    input.r14 = 0x43434343;
    input.r16 = 0x1122334455667788;
    input.hi = 0x12345678;
    input.lo = 0x89ABCDEF;
    input.f6.u64 = 0x432100003F800000;
    input.f18.u64 = 0x8765ABCD40000000;
    input.f20.u64 = 0x123456789ABCDEF0;
    if (argc == 4) {
        input.r5 = std::bit_cast<int32_t>(320.0f);
        input.r6 = std::bit_cast<int32_t>(240.0f);
        input.r7 = std::bit_cast<int32_t>(400.0f);
        if (std::string_view(argv[2]) == "--runtime-mods") {
            MEM_W(0, active_display) = int32_t(descriptor_table);
            wdc::camera::set_output_size(1280, 720);
            wdc::visibility::configure(false, false, 4);
            return test_projection_runtime(argv[1], argv[3], {rdram, ram_size}, input);
        }
        MEM_W(0, active_display) = int32_t(std::strtoul(argv[3], nullptr, 16));
        std::signal(SIGABRT, [](int) { std::_Exit(73); });
        func_80011D54(rdram, &input);
        return 0;
    }
    auto compare_viewport = [&](float width, float height, float distance,
                                wdc::camera::ProjectionPolicy policy, bool controls = false) {
        input.r5 = std::bit_cast<int32_t>(width);
        input.r6 = std::bit_cast<int32_t>(height);
        input.r7 = std::bit_cast<int32_t>(distance);
        compare(initial, reference, candidate, input, policy, controls);
    };

    // Independent stock reconstruction, including direct descriptor rows
    // beyond those reached by the port's layout selector in this fixture.
    for (int mode = 0; mode < 9; ++mode) {
        const gpr descriptor = descriptor_table + mode * 0x74;
        MEM_W(0, active_display) = int32_t(descriptor);
        compare_viewport(float(MEM_W(0, descriptor)), float(MEM_W(4, descriptor)), 400, stock, mode == 0);
    }

    for (const auto& [width, height] : {std::array{960, 720}, {1280, 720}, {720, 960}, {2560, 1080}, {0, 720}}) {
        wdc::camera::set_output_size(width, height);
        reference_width_scale = width > 0 && height > 0
            ? std::max(1.0f, float(width) / float(height) / (4.0f / 3.0f)) : 1.0f;
        for (int mode = 0; mode < 9; ++mode) {
            const gpr descriptor = descriptor_table + mode * 0x74;
            MEM_W(0, active_display) = int32_t(descriptor);
            for (int distance_setting : {1, 2, 3, 4, 1, 0, 5}) {
                wdc::visibility::configure(false, false, distance_setting);
                reference_distance_scale = float(std::clamp(distance_setting, 1, 4));
                compare_viewport(float(MEM_W(0, descriptor)), float(MEM_W(4, descriptor)), 400, enhanced);
            }
        }
    }

    // Run the real selector and viewport calculation for all layout/variant/
    // player combinations and publish the selected descriptor at the frame seam.
    wdc::camera::set_output_size(2560, 1080);
    reference_width_scale = float(2560) / float(1080) / (4.0f / 3.0f);
    wdc::visibility::configure(false, false, 4);
    for (int layout = 0; layout < 8; ++layout)
        for (int variant = 0; variant < 4; ++variant)
            for (int players = 0; players < 3; ++players) {
                MEM_W(0, int32_t(0x8007D758)) = 0;
                MEM_W(0, int32_t(0x8007E578)) = -1;
                MEM_W(0, int32_t(0x8007E57C)) = layout;
                MEM_W(0, int32_t(0x8007E584)) = players;
                MEM_W(0, int32_t(0x8007E5B8)) = variant;
                copy_context(init, input);
                func_8000FC18(rdram, &init);
                MEM_W(0, active_display) = MEM_W(0, int32_t(0x8007D758));
                const auto width = MEM_W(8, scene);
                const auto height = MEM_W(12, scene);
                assert(width > 0 && height > 0);
                compare_viewport(float(width), float(height), 100, enhanced);
                if (players == 2) {
                    const auto right_width = MEM_W(8, second_scene);
                    const auto right_height = MEM_W(12, second_scene);
                    assert(right_width > 0 && right_height > 0);
                    compare_viewport(float(right_width), float(right_height), 100, enhanced);
                }
            }

    constexpr gpr descriptor = camera + 0x100;
    MEM_W(0, active_display) = int32_t(descriptor);
    write_float(rdram, descriptor, 0x14, 2.0f);
    for (const auto& [width, height] : {std::array{0, 240}, {320, 0}, {-1, 240}, {320, -1},
                                                     {4097, 240}, {320, 4097}}) {
        MEM_W(0, descriptor) = width;
        MEM_W(4, descriptor) = height;
        for (float distance : {-2.0f, 0.0f, 400.0f, 1.0e25f})
            compare_viewport(320, 240, distance, enhanced);
    }
    // Exercise the real allocator's low-space path (the ROM diagnostic
    // function spills its arguments and returns, then allocation continues).
    MEM_W(0, alloc_limit) = int32_t(packet);
    compare_viewport(320, 240, 400, enhanced);
}
