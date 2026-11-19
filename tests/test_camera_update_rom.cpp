#undef NDEBUG
#include "game_cheats.h"
#include "game_framerate.h"
#include "recomp.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <vector>

extern "C" {
void func_8000F678(uint8_t*, recomp_context*);
void func_8000FAE8(uint8_t*, recomp_context*);
void func_8001A4C4(uint8_t*, recomp_context*);
void reference_func_8000F678(uint8_t*, recomp_context*);
void reference_func_8000FAE8(uint8_t*, recomp_context*);
void reference_func_8001A4C4(uint8_t*, recomp_context*);
void wdc_framerate_start(uint8_t*, recomp_context*);
}

namespace {
using wdc::cheats::Cheat;
constexpr uint32_t race_callback_slot = 0x8007D848;
constexpr uint32_t race_callback = 0x8000C560;
constexpr uint32_t vehicle = 0x80146000;
constexpr uint32_t stack = 0x807FF000;
constexpr uint32_t update = 0x80012354;
constexpr uint32_t blend = 0x8001A4C4;
constexpr uint32_t vector_add = 0x8001A008;
constexpr std::array<uint32_t, 2> scenes{0x800ABE40, 0x800AE670};
constexpr std::array<unsigned, 5> offsets{0x94, 0x9C, 0xA0, 0xA4, 0xA8};
constexpr uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }
using Words = std::array<uint32_t, offsets.size()>;

uint32_t word(uint8_t* rdram, uint32_t address) {
    return uint32_t(MEM_W(0, int32_t(address)));
}
void write_word(uint8_t* rdram, uint32_t address, uint32_t value) {
    MEM_W(0, int32_t(address)) = value;
}
void vector3(uint8_t* rdram, uint32_t address, float x, float y, float z) {
    write_word(rdram, address, bits(x));
    write_word(rdram, address + 4, bits(y));
    write_word(rdram, address + 8, bits(z));
}
Words read_view(uint8_t* rdram, uint32_t camera) {
    Words values{};
    for (size_t i = 0; i < offsets.size(); ++i) values[i] = word(rdram, camera + offsets[i]);
    return values;
}
void write_view(uint8_t* rdram, uint32_t camera, const Words& values) {
    for (size_t i = 0; i < offsets.size(); ++i) write_word(rdram, camera + offsets[i], values[i]);
}

// Frozen policy. This code is used only by the isolated, independently
// translated reference. The production hooks run on the other RDRAM bank.
struct Snapshot {
    uint8_t* rdram;
    uint32_t camera;
    Words values;
};
std::optional<Snapshot> snapshot;
bool reference_top_down;
bool reference_racing_60;

using Registers = std::array<uint8_t, offsetof(recomp_context, f_odd)>;
Registers registers(const recomp_context& ctx) {
    Registers result{};
    std::copy_n(reinterpret_cast<const uint8_t*>(&ctx), result.size(), result.begin());
    return result;
}

struct Boundary {
    uint32_t address;
    Registers registers;
    uint32_t f0;
    std::optional<std::array<uint32_t, 3>> target_offset;
    std::array<uint8_t, 0x2E4> scene_bytes;
    std::array<uint8_t, 0x180> stack_bytes;
    bool operator==(const Boundary&) const = default;
};
struct Trace {
    bool reference;
    uint32_t scene;
    bool toggle_at_blend;
    std::vector<Boundary> calls{};
    std::optional<Words> authored_view{};
};
Trace* current_trace;
}

extern "C" void camera_reference_end(uint8_t* rdram, recomp_context*) {
    if (snapshot && snapshot->rdram == rdram) write_view(rdram, snapshot->camera, snapshot->values);
    snapshot.reset();
}

extern "C" void camera_reference_begin(uint8_t* rdram, recomp_context* ctx) {
    if (!reference_top_down) return;
    camera_reference_end(rdram, ctx);
    if (!rdram || !ctx) return;
    const uint32_t camera = uint32_t(ctx->r4);
    if (ctx->r4 != camera && ctx->r4 != uint64_t(int64_t(int32_t(camera)))) return;
    if (std::ranges::find(scenes, camera - 0x11C) == scenes.end() ||
        word(rdram, race_callback_slot) != race_callback) return;
    const float scale = std::bit_cast<float>(word(rdram, camera + 0x8C));
    const float distance = 13.0f * scale;
    const float height = 11.0f * scale;
    if (!(scale > 0.0f) || !std::isfinite(scale) || !std::isfinite(distance) || !std::isfinite(height)) return;
    snapshot = Snapshot{rdram, camera, read_view(rdram, camera)};
    write_view(rdram, camera, {bits(distance), bits(height), 0, 0, bits(4.0f)});
}

extern "C" void camera_reference_rate(uint8_t*, recomp_context* ctx) {
    if (reference_racing_60 && uint32_t(ctx->r31) == 0x800124F0 && uint32_t(ctx->r7) == bits(0.25f))
        ctx->r7 = 0x3E0930A4; // float32 result of 1.0f - sqrtf(0.75f)
}

extern "C" void camera_update_trace(int reference, uint32_t address, uint8_t* rdram, recomp_context* ctx) {
    assert(current_trace && current_trace->reference == (reference != 0));
    auto& trace = *current_trace;
    auto& entry = trace.calls.emplace_back();
    entry.address = address;
    entry.registers = registers(*ctx);
    entry.f0 = ctx->f0.u32l;
    const uint32_t camera = trace.scene + 0x11C;
    const Words view = read_view(rdram, camera);
    if (address == update) trace.authored_view = view;
    if (!reference && trace.authored_view) assert(view == *trace.authored_view);
    if (address == blend && ctx->r16 != 0) {
        assert(uint32_t(ctx->r16) == camera);
        assert(uint32_t(ctx->r31) == 0x800124F0);
        assert(ctx->r4 == ctx->r6);
    }
    std::copy_n(rdram + (trace.scene - 0x80000000), entry.scene_bytes.size(), entry.scene_bytes.begin());
    constexpr uint32_t stack_begin = stack - 0x160;
    std::copy_n(rdram + (stack_begin - 0x80000000), entry.stack_bytes.size(), entry.stack_bytes.begin());
    if (reference && snapshot) {
        // Reference-only transient writes. Keep the rest of the scene intact.
        assert(trace.authored_view);
        for (size_t i = 0; i < offsets.size(); ++i) {
            const auto bits = (*trace.authored_view)[i];
            std::copy_n(reinterpret_cast<const uint8_t*>(&bits), sizeof(bits),
                        entry.scene_bytes.begin() + 0x11C + offsets[i]);
        }
    }
    if (address == vector_add && trace.authored_view && ctx->r4 == ADD32(ctx->r29, 0x64)) {
        const gpr authored = ADD32(ctx->r16, 0xA0);
        assert(ctx->r6 == ADD32(ctx->r16, 0xAC));
        assert(ctx->r5 == authored || (!reference && ctx->r5 == ctx->r4));
        const uint32_t source = uint32_t(ctx->r5);
        entry.target_offset = std::array{word(rdram, source), word(rdram, source + 4), word(rdram, source + 8)};
        if (ctx->r5 != authored || (reference && snapshot)) {
            // The re-used destination is completely replaced before the next
            // call. Compare its effective input above and final bytes later.
            std::copy_n(reinterpret_cast<const uint8_t*>(&authored), sizeof(authored),
                        entry.registers.begin() + offsetof(recomp_context, r5));
            std::fill_n(entry.stack_bytes.begin() + (uint32_t(ctx->r4) - stack_begin), 12, uint8_t{});
        }
    }
    if (address == blend && trace.toggle_at_blend) {
        if (reference) {
            reference_top_down = false;
        } else {
            wdc::cheats::set_enabled(Cheat::TopDown, false);
            wdc::framerate::configure(false); // The active race-rate latch still holds.
        }
    }
}

namespace {
struct Scenario {
    bool initializer = false;
    bool top_down = true;
    bool racing = true;
    bool rate_60 = false;
    bool zero_position = false;
    bool snap_direction = false;
    bool cut = false;
    bool zero_heading = false;
    bool same_position = false;
    bool obstruction = false;
    bool obstruction_hit = false;
    bool has_vehicle = true;
    bool toggle_at_blend = false;
    bool arbitrary_view_words = false;
    bool nonlocal = false;
    uint32_t scale = bits(2.0f);
};

void setup(uint8_t* rdram, uint32_t scene, const Scenario& scenario) {
    const uint32_t camera = scene + 0x11C;
    write_word(rdram, race_callback_slot, scenario.racing ? race_callback : 0x8004E950);
    write_word(rdram, 0x800923E4, 8);
    write_word(rdram, 0x8007DF40, scenario.obstruction);
    vector3(rdram, 0x8007E6E0, 0.0f, 0.0f, 1.0f);
    write_word(rdram, scene + 0x2E0, scenario.has_vehicle ? vehicle : 0);
    write_word(rdram, camera + 0x80, vehicle + 0x14);
    write_word(rdram, camera + 0x88, bits(1.5f));
    write_word(rdram, camera + 0x8C, scenario.scale);
    write_word(rdram, camera + 0xF8, scenario.cut);
    write_word(rdram, camera + 0x104, bits(0.25f));
    write_view(rdram, camera, {bits(3.0f), bits(5.0f),
        scenario.arbitrary_view_words ? 0x7FC12345 : bits(1.0f), 0x80000000, bits(9.0f)});
    vector3(rdram, camera + 0x30, scenario.zero_position ? 0.0f : 4.0f,
            scenario.zero_position ? 0.0f : 8.0f, scenario.zero_position ? -0.0f : 10.0f);
    vector3(rdram, camera + 0xAC, 2.0f, 3.0f, 1.0f);
    vector3(rdram, camera + 0xB8, 0.0f, 1.0f, 0.0f);
    vector3(rdram, camera + 0xE8, 3.0f, 6.0f, 9.0f);
    vector3(rdram, vehicle + 0x14, scenario.zero_heading ? 0.0f : 1.0f, 0.0f,
            scenario.zero_heading ? -0.25f : 0.25f);
    vector3(rdram, vehicle + 0x44, scenario.same_position ? 3.0f : 12.0f, 6.0f, 9.0f);
    write_word(rdram, vehicle + 0x2DC, bits(1.0f));
    write_word(rdram, vehicle + 0xF18, 49);
    write_word(rdram, vehicle + 0xF1C, 1);
    // Exercise two obstruction segments across ring wrap.
    const float segment_x = scenario.obstruction_hit ? 8.0f : 30.0f;
    vector3(rdram, vehicle + 0xF20 + 49 * 0x30, segment_x, -10.0f, 0.0f);
    vector3(rdram, vehicle + 0xF20 + 49 * 0x30 + 0xC, segment_x, 10.0f, 0.0f);
    vector3(rdram, vehicle + 0xF20, 40.0f, -10.0f, 0.0f);
    vector3(rdram, vehicle + 0xF20 + 0xC, 40.0f, 10.0f, 0.0f);
}

void configure_rate(uint8_t* rdram, bool enabled) {
    wdc::framerate::configure(enabled);
    recomp_context start{};
    start.r4 = int32_t(0x8000C0D0);
    start.r5 = 1;
    wdc_framerate_start(rdram, &start);
    assert(start.r5 == !enabled);
}

void invoke(uint8_t* rdram, recomp_context& ctx, Trace& trace, const Scenario& scenario) {
    const uint32_t scene = trace.scene;
    ctx.r29 = int32_t(stack);
    ctx.r31 = int32_t(0x80001000);
    ctx.r16 = int32_t(0x80001004);
    ctx.f_odd = &ctx.f0.u32h; // The runtime's FR=0 binding within this bank.
    current_trace = &trace;
    if (scenario.initializer) {
        ctx.r4 = int32_t(scene);
        ctx.r5 = int32_t(vehicle);
        (trace.reference ? reference_func_8000FAE8 : func_8000FAE8)(rdram, &ctx);
    } else {
        // The translated slice begins at 8000F678 with the dispatcher's
        // saved arguments in its caller-owned home slots.
        write_word(rdram, stack + 0x130, scene);
        write_word(rdram, stack + 0x134, scenario.snap_direction);
        (trace.reference ? reference_func_8000F678 : func_8000F678)(rdram, &ctx);
    }
    current_trace = nullptr;
    assert(ctx.r29 == uint64_t(int64_t(int32_t(stack))));
    assert(ctx.r31 == 0xFFFFFFFF80001000 && ctx.r16 == 0xFFFFFFFF80001004);
    assert(ctx.f_odd == &ctx.f0.u32h);
}

void compare(const std::vector<uint8_t>& rom, uint32_t scene, const Scenario& scenario) {
    if (scenario.nonlocal) scene = 0x80120000;
    auto production = rom;
    setup(production.data(), scene, scenario);
    auto reference = production;
    recomp_context prod_ctx{}, ref_ctx{};
    configure_rate(production.data(), scenario.racing && scenario.rate_60);
    reference_racing_60 = scenario.racing && scenario.rate_60;
    wdc::cheats::set_enabled(Cheat::TopDown, scenario.top_down);
    reference_top_down = scenario.top_down;

    // A second update starts from the real first result, including cut and
    // settings changes during the first update and the same restored fields.
    const uint32_t camera = scene + 0x11C;
    for (bool first : {true, false}) {
        Trace prod_trace{false, scene, scenario.toggle_at_blend};
        Trace ref_trace{true, scene, scenario.toggle_at_blend};
        invoke(production.data(), prod_ctx, prod_trace, scenario);
        invoke(reference.data(), ref_ctx, ref_trace, scenario);
        assert(!snapshot);
        assert(prod_trace.calls == ref_trace.calls);
        assert(registers(prod_ctx) == registers(ref_ctx));
        assert(prod_ctx.status_reg == ref_ctx.status_reg && prod_ctx.mips3_float_mode == ref_ctx.mips3_float_mode);
        assert(production == reference); // Includes every stack byte and unrelated RAM.

        assert(prod_trace.authored_view && prod_trace.authored_view == ref_trace.authored_view);
        assert(read_view(production.data(), camera) == *prod_trace.authored_view);
        const auto blend_call = std::ranges::find(prod_trace.calls, blend, &Boundary::address);
        if (first) {
            const bool uses_blend = !scenario.zero_position && !scenario.cut && !scenario.snap_direction;
            assert((blend_call != prod_trace.calls.end()) == uses_blend);
            const auto segments = std::ranges::count(prod_trace.calls, 0x80014D60, &Boundary::address);
            assert(segments == (scenario.obstruction && scenario.has_vehicle ? 2 : 0));
            const auto corrections = std::ranges::count(prod_trace.calls, 0x8001A1C4, &Boundary::address);
            assert(corrections == (scenario.obstruction_hit ? 1 : 0));
        }
        if (blend_call != prod_trace.calls.end()) {
            const auto next = blend_call + 1;
            assert(next != prod_trace.calls.end() && next->address == 0x8001A520);
            const uint32_t coefficient = scenario.racing && scenario.rate_60 ? 0x3E0930A4 : bits(0.25f);
            assert(next->f0 == coefficient);
        }
    }
}

void check_blend_other_link(const std::vector<uint8_t>& rom) {
    auto production = rom;
    auto* rdram = production.data();
    configure_rate(rdram, true);
    reference_racing_60 = true;
    constexpr uint32_t a = stack + 4, b = stack + 12;
    vector3(rdram, a, 20.0f, 40.0f, 0.0f);
    vector3(rdram, b, 4.0f, 8.0f, 0.0f);
    auto reference = production;
    for (bool frozen : {false, true}) {
        Trace trace{frozen, scenes[0], false};
        current_trace = &trace;
        recomp_context ctx{};
        ctx.r31 = int32_t(0x80012510);
        ctx.r4 = ctx.r6 = int32_t(b);
        ctx.r5 = int32_t(a);
        ctx.r7 = bits(0.25f);
        auto* bank = frozen ? reference.data() : production.data();
        (frozen ? reference_func_8001A4C4 : func_8001A4C4)(bank, &ctx);
        assert(word(bank, b) == bits(8.0f) && word(bank, b + 4) == bits(16.0f));
        assert(ctx.r7 == bits(0.25f));
        current_trace = nullptr;
    }
    assert(production == reference);
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream rom(argv[1], std::ios::binary);
    rom.seekg(0x1000);
    std::vector<char> data(0x100000);
    rom.read(data.data(), data.size());
    assert(rom);
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    for (size_t i = 0; i < data.size(); ++i) MEM_B(i, int32_t(0x80000400)) = data[i];

    for (uint32_t scene : scenes) {
        for (bool top_down : {false, true}) for (bool rate_60 : {false, true}) {
            compare(memory, scene, {.top_down = top_down, .rate_60 = rate_60});
            compare(memory, scene, {.initializer = true, .top_down = top_down, .rate_60 = rate_60});
        }
        compare(memory, scene, {.racing = false});
        compare(memory, scene, {.zero_position = true});
        compare(memory, scene, {.snap_direction = true});
        compare(memory, scene, {.cut = true});
        compare(memory, scene, {.zero_heading = true, .same_position = true});
        compare(memory, scene, {.obstruction = true});
        compare(memory, scene, {.obstruction = true, .obstruction_hit = true});
        compare(memory, scene, {.obstruction = true, .has_vehicle = false});
        compare(memory, scene, {.rate_60 = true, .toggle_at_blend = true});
        compare(memory, scene, {.arbitrary_view_words = true});
        for (uint32_t scale : {0u, bits(-1.0f), 0x7F800000u, 0x7FC12345u, 0x7F7FFFFFu})
            compare(memory, scene, {.scale = scale});
    }
    compare(memory, scenes[0], {.nonlocal = true});
    check_blend_other_link(memory);
}
