#pragma once
#include "recomp.h"

namespace wdc {

// recomp_context contains a self-pointer. Rebind AFTER every copy, at its final
// address (returning a rebound context by value would depend on copy elision).
// Match librecomp's cop0_status_write FR=0/FR=1 register-bank selection.
inline void copy_guest_context(recomp_context& destination, const recomp_context& source) {
    destination = source;
    destination.f_odd = destination.mips3_float_mode ?
        &destination.f1.u32l : &destination.f0.u32h;
}

} // namespace wdc
