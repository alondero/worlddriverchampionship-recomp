#undef NDEBUG
#include "game_grid.h"
#include "recomp.h"
#include <cassert>
#include <utility>
#include <vector>

int main() {
    using namespace wdc::grid;
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    constexpr int32_t config = int32_t(0x80092360);
    constexpr int32_t retry = int32_t(0x80099B54);
    using Race = std::array<uint32_t, 0xA0 / 4>;
    const auto read = [&](int32_t address) {
        Race race{};
        for (int i = 0; i < int(race.size()); ++i) race[i] = MEM_W(i * 4, address);
        return race;
    };
    const auto write = [&](const Race& race, int32_t address) {
        for (int i = 0; i < int(race.size()); ++i) MEM_W(i * 4, address) = race[i];
    };
    constexpr size_t cars = 0x08 / 4, colors = 0x28 / 4, positions = 0x48 / 4, mode = 0x84 / 4;
    Race native{};
    native.fill(0xBAADF00D);
    native[0] = 1;
    native[1] = 8;
    native[mode] = 3;
    for (uint32_t i = 0; i < 8; ++i) {
        native[cars + i] = i + 1;
        native[colors + i] = 1;
        native[positions + i] = 7 - i;
    }
    write(native, retry);
    write(native, config);
    wdc_apply_custom_grid(rdram);
    assert(read(config) == native);

    const Grid custom{{{34, 2}, {33, 0}, {32, 1}, {31, 2},
                       {original, 0}, {0, original}, {2, 2}}};
    assert(set(custom));
    auto invalid = custom;
    invalid[0].car = car_count;
    assert(!set(invalid));
    invalid = custom;
    invalid[6].color = color_count;
    assert(!set(invalid));
    wdc_apply_custom_grid(nullptr);
    wdc_apply_custom_grid(rdram);
    auto expected = native;
    for (size_t i = 0; i < custom.size(); ++i) {
        if (custom[i].car != original) expected[cars + 7 - i] = custom[i].car;
        if (custom[i].color != original) expected[colors + 7 - i] = custom[i].color;
    }
    assert(read(config) == expected && read(retry) == native);

    // Reapply after a native retry restores the original setup.
    write(read(retry), config);
    wdc_apply_custom_grid(rdram);
    assert(read(config) == expected);
    assert(set({}));
    write(read(retry), config);
    wdc_apply_custom_grid(rdram);
    assert(read(config) == native);

    // A two-car Championship setup can place the human ahead of the CPU.
    assert(set(custom));
    auto duel = native;
    duel[mode] = 2;
    duel[1] = 2;
    duel[positions] = 0;
    duel[positions + 1] = 1;
    write(duel, config);
    wdc_apply_custom_grid(rdram);
    duel[cars + 1] = 34;
    duel[colors + 1] = 2;
    assert(read(config) == duel);

    for (const auto race_mode : {0u, 4u, 5u, 8u, 10u, 11u}) {
        auto other = native;
        other[mode] = race_mode;
        write(other, config);
        wdc_apply_custom_grid(rdram);
        assert(read(config) == other);
    }
    for (const auto [field, bad] : {std::pair{size_t{0}, 2u}, {size_t{1}, 1u},
                                   {size_t{1}, 9u}, {positions, 8u}, {positions, 0u}}) {
        auto broken = native;
        broken[field] = bad;
        write(broken, config);
        wdc_apply_custom_grid(rdram);
        assert(read(config) == broken);
    }
}
