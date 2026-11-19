#undef NDEBUG
#include "game_projection.h"
#include "librecomp/mods.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

void register_overlays();
// Set by recomp::start() in a player. The harness starts only the mod loader.
extern recomp::Version project_version;

int test_projection_runtime(const char* rom_path, const char* mods_path,
                            std::span<const uint8_t> initial, const recomp_context& input) {
    constexpr uint32_t section_rom = 0x1000;
    constexpr uint32_t section_vram = 0x80000400;
    constexpr uint32_t projection_vram = 0x80011D54;
    constexpr uint32_t control_vram = 0x800006E0;
    constexpr gpr trace = int32_t(0x80410000);
    constexpr gpr matrix = int32_t(0x800B2A80);
    constexpr gpr normalization = int32_t(0x8007ED64);
    constexpr gpr published = int32_t(0x8007ED68);
    namespace overlays = recomp::overlays;
    namespace mods = recomp::mods;

    register_overlays();
    overlays::init_overlays();
    load_overlays(section_rom, int32_t(section_vram), 0x7C2B0);
    const auto indirect = get_function(int32_t(projection_vram));
    assert(indirect == func_80011D54);
    assert(indirect == overlays::get_func_by_section_rom_function_vram(section_rom, projection_vram));
    assert(!overlays::get_func_by_section_rom_function_vram(section_rom, projection_vram + 4));
    assert(!overlays::get_func_by_section_rom_function_vram(section_rom + 4, projection_vram));
    FuncEntry entry{};
    const auto section = overlays::get_vrom_to_section_map().at(section_rom);
    assert(overlays::get_func_entry_by_section_index_function_offset(section, projection_vram - section_vram, entry));
    assert(entry.func == indirect && entry.rom_size == 0);

    std::vector<uint8_t> memory(initial.begin(), initial.end());
    auto* rdram = memory.data();
    auto reset_context = [&](recomp_context& ctx) {
        std::copy(initial.begin(), initial.end(), memory.begin());
        ctx = input;
        ctx.f_odd = &ctx.f0.u32h;
    };
    auto projection_result = [&](recomp_func_t* fn) {
        recomp_context ctx{};
        reset_context(ctx);
        fn(rdram, &ctx);
        assert(ctx.r29 == input.r29 && ctx.r31 == input.r31);
        assert(ctx.r2 == uint64_t(int32_t(MEM_W(0, published))));
        return std::array<uint32_t, 4>{uint32_t(MEM_W(0x7C, input.r4)), uint32_t(MEM_W(0x28, matrix)),
                                     uint16_t(MEM_H(0, normalization)), uint32_t(MEM_W(0, published))};
    };
    const auto expected = projection_result(func_80011D54);
    assert(expected[0] && expected[1] && expected[2] == 10 && expected[3]);
    auto check_projection = [&] {
        const auto current = get_function(int32_t(projection_vram));
        assert(current == indirect);
        assert(projection_result(func_80011D54) == expected);
        assert(projection_result(current) == expected);
    };
    auto control = [&] {
        recomp_context ctx{};
        reset_context(ctx);
        get_function(int32_t(control_vram))(rdram, &ctx);
        return uint32_t(MEM_W(0, trace));
    };
    check_projection();
    assert(control() == 0);

    std::ifstream stream(rom_path, std::ios::binary);
    std::vector<uint8_t> rom{std::istreambuf_iterator<char>(stream), {}};
    assert(rom.size() == 0x1000000);
    recomp::set_rom_contents(std::move(rom));
    project_version = recomp::Version(1, 0, 0);
    const recomp::GameEntry game{.mod_game_id = "wdc-projection-test"};
    mods::ModContext context;
    context.register_game(game.mod_game_id);
    assert(context.scan_mod_folder(mods_path).empty());
    assert(context.num_opened_mods() > 0);
    context.set_mods_config_path(std::filesystem::path(mods_path) / "mods.json");
    context.load_mods_config();
    uint32_t used{};
    const auto errors = context.load_mods(game, "", rdram, int32_t(0x80500000), used);
    for (const auto& error : errors)
        std::cout << mods::error_to_string(error.error) << ' ' << error.error_param << '\n';

    using Error = mods::CodeModLoadError;
    const auto scenario = std::filesystem::path(mods_path).filename().string();
    auto expect_error = [&](Error error) {
        assert(errors.size() == 1);
        assert(errors.front().error == mods::ModLoadError::FailedToLoadCode);
        assert(errors.front().error_param.starts_with(mods::error_to_string(error)));
        assert(control() == 0);
    };
    if (scenario == "entry-hook" || scenario == "return-hook") {
        expect_error(Error::CannotBeHooked);
        assert(errors.front().error_param.ends_with("section: 0x1000 func: 0x80011d54"));
    } else if (scenario.ends_with("-projection")) {
        expect_error(Error::CannotBeReplaced);
        assert(errors.front().error_param.ends_with("section: 0x1000 func: 0x80011d54"));
    } else if (scenario == "duplicate") {
        expect_error(Error::ModConflict);
    } else if (scenario == "offline-conflict") {
        expect_error(Error::OfflineModHooked);
    } else {
        assert(scenario == "replace" || scenario == "force" || scenario == "hook-order");
        assert(errors.empty());
        assert(control() == (scenario == "hook-order" ? 0x123u : 2u));
    }
    check_projection();
    context.unload_mods();
    assert(control() == 0);
    check_projection();
    return 0;
}
