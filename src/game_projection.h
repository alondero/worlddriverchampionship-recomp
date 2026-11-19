#pragma once

#include "recomp.h"

#ifdef __cplusplus
extern "C"
#endif
void func_80011D54(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
namespace wdc::camera {

enum class ProjectionPolicy { Stock, Enhanced };

// The production entry uses Enhanced. Stock is exercised by the independent
// USA ROM differential fixture.
void setup_projection(uint8_t* rdram, recomp_context* ctx, ProjectionPolicy policy);

}
#endif
