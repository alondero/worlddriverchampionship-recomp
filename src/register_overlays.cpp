#include "recomp_overlays.inl"
#include "librecomp/overlays.hpp"

void register_overlays() {
    recomp::overlays::register_overlays(
        {.code_sections = section_table,
         .num_code_sections = sizeof(section_table) / sizeof(section_table[0]),
         .total_num_sections = num_sections},
        {.table = overlay_sections_by_index,
         .len = sizeof(overlay_sections_by_index) / sizeof(overlay_sections_by_index[0])});
}
