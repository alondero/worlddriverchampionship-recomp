#include "game_projection.h"

#include "game_camera.h"
#include "game_visibility.h"
#include "guest_memory.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>

extern "C" {
void func_8001A864(uint8_t*, recomp_context*);
void func_80070DB0(uint8_t*, recomp_context*);
void func_8001A79C(uint8_t*, recomp_context*);
void func_800710E0(uint8_t*, recomp_context*);
void func_8002CF30(uint8_t*, recomp_context*);
}

namespace {
constexpr gpr active_display = int32_t(0x8007D75C);
constexpr gpr view_matrix = int32_t(0x800B2AC0);
constexpr gpr projection_matrix = int32_t(0x800B2A80);
constexpr gpr combined_matrix = int32_t(0x800B2A40);
constexpr gpr normalization = int32_t(0x8007ED64);
constexpr gpr published_matrix = int32_t(0x8007ED68);
constexpr gpr half_angle_to_fov_degrees = int32_t(0x8009A620);
constexpr float world_to_boss = 32.0f;
constexpr float original_aspect = 4.0f / 3.0f;
constexpr int32_t max_display_dimension = 4096;

float guest_float(const uint8_t* rdram, gpr address, int offset = 0) {
    return std::bit_cast<float>(uint32_t(MEM_W(offset, address)));
}

gpr float_argument(float value) {
    return std::bit_cast<int32_t>(value);
}

float enhanced_aspect(const uint8_t* rdram, gpr descriptor, float viewport_aspect, float rom_aspect) {
    const int32_t width = MEM_W(0, descriptor);
    const int32_t height = MEM_W(4, descriptor);
    if (width > 0 && width <= max_display_dimension && height > 0 && height <= max_display_dimension)
        return viewport_aspect * (float(height) / float(width)) * original_aspect * wdc::camera::width_scale();

    static std::atomic_flag warned;
    if (!warned.test_and_set())
        std::fprintf(stderr, "[wdc] invalid camera display size: %d x %d; scaling ROM aspect\n", width, height);
    return rom_aspect * wdc::camera::width_scale();
}
}

// See the projection contract in docs/reference/renderer.md for the guest
// fields, helper effects and supported input domain. The translated helpers
// still take the original o32 arguments in the current game-thread context.
void wdc::camera::setup_projection(uint8_t* rdram, recomp_context* ctx, ProjectionPolicy policy) {
    const bool enhanced = policy == ProjectionPolicy::Enhanced;
    const gpr camera = ctx->r4;
    const float viewport_width = std::bit_cast<float>(uint32_t(ctx->r5));
    const float viewport_height = std::bit_cast<float>(uint32_t(ctx->r6));
    const float original_distance = std::bit_cast<float>(uint32_t(ctx->r7));
    const gpr caller_stack = ctx->r29;
    const gpr return_address = ctx->r31;
    wdc_require_guest_range(camera, 0x80, 4);

    // Retain the guest frame depth for existing helpers. The only caller
    // stores are the three outgoing perspective arguments required by o32.
    ctx->r29 = ADD32(caller_stack, -0x28);
    ctx->r4 = view_matrix;
    ctx->r5 = camera;
    func_8001A864(rdram, ctx);

    const gpr descriptor = MEM_W(0, active_display);
    wdc_require_guest_range(descriptor, 0x18, 4);
    const float viewport_aspect = viewport_width / viewport_height;
    const float rom_aspect = viewport_aspect * guest_float(rdram, descriptor, 0x14);
    const float aspect = enhanced ? enhanced_aspect(rdram, descriptor, viewport_aspect, rom_aspect) : rom_aspect;
    const float horizontal_extent = guest_float(rdram, camera, 0x74) * aspect;
    MEM_W(0x7C, camera) = float_argument(horizontal_extent);

    const float fov_degrees = guest_float(rdram, camera, 0x68) * guest_float(rdram, half_angle_to_fov_degrees);
    const float far_distance = enhanced ? original_distance * wdc::visibility::distance_scale() : original_distance;
    const float far_plane = far_distance * world_to_boss;
    MEM_W(0x10, ctx->r29) = float_argument(world_to_boss);
    MEM_W(0x14, ctx->r29) = float_argument(far_plane);
    MEM_W(0x18, ctx->r29) = float_argument(1.0f);
    ctx->r4 = projection_matrix;
    ctx->r5 = normalization;
    ctx->r6 = float_argument(fov_degrees);
    ctx->r7 = float_argument(aspect);
    func_80070DB0(rdram, ctx);

    // BOSS uses perspNorm to clamp projected coordinates as well as for
    // reciprocal W. Preserve its original range while extending the matrix.
    if (enhanced && std::isfinite(original_distance) && original_distance >= 0) {
        const float sum = world_to_boss + original_distance * world_to_boss;
        MEM_H(0, normalization) = uint16_t(std::clamp(131072.0 / double(sum), 1.0, 65535.0));
    }

    ctx->r4 = view_matrix;
    ctx->r5 = float_argument(world_to_boss);
    func_8001A79C(rdram, ctx);
    ctx->r4 = view_matrix;
    ctx->r5 = projection_matrix;
    ctx->r6 = combined_matrix;
    func_800710E0(rdram, ctx);
    ctx->r4 = combined_matrix;
    func_8002CF30(rdram, ctx);
    MEM_W(0, published_matrix) = ctx->r2;

    ctx->r1 = int32_t(0x80080000);
    ctx->r29 = caller_stack;
    ctx->r31 = return_address;
}
