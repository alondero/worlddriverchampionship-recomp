#include "librecomp/sections.h"

extern "C" void func_800006E0(uint8_t*, recomp_context*);

// Minimal USA section table with one ordinary ROM function as a mod control.
// Production register_overlays() adds the projection entry itself.
static FuncEntry functions[] = {
    {.func = func_800006E0, .offset = 0x2E0, .rom_size = 0x9C0},
};
static SectionTableEntry section_table[] = {
    {.rom_addr = 0x1000, .ram_addr = 0x80000400, .size = 0x7C2B0,
     .funcs = functions, .num_funcs = 1, .index = 0},
};
static int overlay_sections_by_index[] = {0};
static constexpr unsigned num_sections = 1;
