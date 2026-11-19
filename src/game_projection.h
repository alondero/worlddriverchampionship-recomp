#pragma once

#include "recomp.h"

namespace wdc::camera {

enum class ProjectionPolicy { Stock, Enhanced };

// Recovered 80011D54 body. The ROM fixture exercises both policies before
// this body is registered at the original game entry.
void setup_projection(uint8_t* rdram, recomp_context* ctx, ProjectionPolicy policy);

}
