#pragma once
#include "recomp.h"
// N64Recomp omits config-ignored entries from its generated declarations.
#include "game_projection.h"

// The pinned recompiler emits MEM_WU for MIPS lwu; this runtime revision
// supplies only signed MEM_W. Preserve zero-extension to 64-bit GPRs.
#ifndef MEM_WU
#define MEM_WU(offset, addr) ((uint32_t)MEM_W(offset, addr))
#endif
