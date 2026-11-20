#pragma once

#include "render_bank.h"
#include "render_pose.h"
#include "render_vector.h"
#include <optional>

namespace wdc::render {
struct PoseContinuity {
    bool identity_changed, count_changed, shadow_count_changed;
    uint32_t sample_gap;
    bool pose_jump = false, shadow_jump = false;
    bool reset() const {
        return identity_changed || count_changed || shadow_count_changed || !sample_gap ||
               sample_gap > 4 || pose_jump || shadow_jump;
    }
};

struct PoseChanges {
    unsigned camera = 0, bodies = 0, wheels = 0, shadows = 0;
};
// Controls which rigid matrices move. Captured shadow inputs always follow alpha.
enum class PoseBlend { All, WheelsOnly, ShadowsOnly };
class RacePoses {
public:
    // Read the frozen USA race image on the game thread. An unsupported view,
    // address or invalid rigid/ground input leaves no usable sample.
    static std::optional<RacePoses> capture(const RenderBank& bank, bool wheels, bool shadows);
    PoseContinuity compare_with(const RacePoses& previous) const;

    // Caller holds the bank mutex after restore. On discontinuities pass this
    // sample as previous. On failure discard the bank and restore before draw.
    std::optional<PoseChanges> apply(RenderBank& bank, const RacePoses& previous, double alpha,
                                     PoseBlend blend = PoseBlend::All) const;
    std::size_t pose_count() const { return poses.size(); }
    std::size_t shadow_count() const { return shadow_fields.size(); }
    bool uses_contact_shadows() const { return contact_shadows; }
    const std::vector<uint32_t>& identity_words() const { return identity; }

private:
    enum class PoseKind { Camera, Body, Wheel };
    struct PoseField { uint32_t address; PoseKind kind; };
    struct ShadowField { uint32_t address; unsigned words; bool normal; };
    std::vector<PoseField> pose_fields;
    std::vector<Pose> poses;
    std::vector<ShadowField> shadow_fields;
    std::vector<Vector3> shadow_inputs;
    std::vector<uint32_t> identity;
    bool contact_shadows = false;
    uint32_t simulation_sample = 0;
};
}
