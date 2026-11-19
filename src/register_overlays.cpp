#include "recomp_overlays.inl"
#include "librecomp/overlays.hpp"
#include "game_projection.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <vector>

namespace {
void register_projection_entry() {
    constexpr uint32_t rom = 0x1000;
    constexpr uint32_t vram = 0x80011D54;
    auto* section = std::ranges::find(section_table, rom, &SectionTableEntry::rom_addr);
    if (section == std::end(section_table) || vram < section->ram_addr ||
        vram - section->ram_addr >= section->size) {
        std::fputs("[wdc] Missing original projection section\n", stderr);
        std::abort();
    }
    const auto offset = vram - section->ram_addr;
    auto* end = section->funcs + section->num_funcs;
    if (std::ranges::find(section->funcs, end, offset, &FuncEntry::offset) != end) {
        std::fputs("[wdc] Duplicate function at projection entry\n", stderr);
        std::abort();
    }

    // Keep exact lookup in the original section. A zero ROM size prevents
    // runtime hooks from translating the stock ROM over this native body.
    static std::vector<FuncEntry> functions(section->funcs, end);
    functions.push_back({.func = func_80011D54, .offset = offset, .rom_size = 0});
    section->funcs = functions.data();
    section->num_funcs = functions.size();
    recomp::overlays::protect_function_from_replacement(func_80011D54);
}
}

void register_overlays() {
    register_projection_entry();
    recomp::overlays::register_overlays(
        {.code_sections = section_table,
         .num_code_sections = sizeof(section_table) / sizeof(section_table[0]),
         .total_num_sections = num_sections},
        {.table = overlay_sections_by_index,
         .len = sizeof(overlay_sections_by_index) / sizeof(overlay_sections_by_index[0])});
}
