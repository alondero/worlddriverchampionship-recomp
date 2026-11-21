#include "race_poses.h"
#include "race_globals.h"
#include <cstring>

namespace wdc::render {
std::optional<RacePoses> RacePoses::capture(const RenderBank& bank, bool wheels, bool shadows) {
    const auto* frozen = bank.snapshot();
    if (!frozen) return std::nullopt;
    constexpr uint32_t base = 0x80000000, end = base + RenderBank::size;
    constexpr uint32_t view = 0x800ABE40, first_car = 0x800BA7F0, car_stride = 0x18A0;
    // Aligned 32-bit guest words use host byte order in the word-swapped image.
    const auto word = [frozen](uint32_t address) {
        return race::word(frozen, address);
    };
    const uint32_t camera = word(view + 0x2DC);
    const uint32_t cars = word(race::cars);
    if (word(race::views) != 1 || camera < base || camera > end - 0x80 || (camera & 3) ||
        !cars || cars > (end - first_car) / car_stride) return std::nullopt;

    RacePoses result;
    result.simulation_sample = word(race::sample);
    result.pose_fields.push_back({camera, PoseKind::Camera});
    result.identity = {word(race::scene), camera, word(view + 0x74)};
    if (shadows) {
        const uint32_t path = word(0x8007DF40);
        result.contact_shadows = path != 0;
        result.identity.push_back(path);
    }
    for (unsigned i = 0; i < cars; ++i) {
        const uint32_t car = first_car + i * car_stride;
        result.pose_fields.push_back({car + 0x14, PoseKind::Body});
        if (wheels) {
            for (unsigned wheel = 0; wheel < 4; ++wheel)
                result.pose_fields.push_back({car + 0x94 + wheel * 0x84, PoseKind::Wheel});
        }
        result.identity.push_back(word(car));
        if (!shadows) continue;
        // 30644 tests for ground contact. The contact reference itself can
        // change every simulation tick as the car travels across the track.
        const bool active = word(car + 0x2F4) != 0;
        result.identity.push_back(active);
        if (!active) continue;
        if (!result.contact_shadows) result.shadow_fields.push_back({car + 0x360, 3, true});
        result.shadow_fields.push_back({car + 0x36C, result.contact_shadows ? 2u : 3u, false});
        if (result.contact_shadows) {
            for (unsigned j = 0; j < 4; ++j)
                result.shadow_fields.push_back({car + 0xC6C + 12 * j, 3, false});
        }
    }
    // 218F0 consumes the 64-byte matrices. 3062C consumes world-space
    // float32 positions or a unit normal. Preserve opaque words verbatim.
    for (const auto field : result.pose_fields) {
        Pose pose;
        std::memcpy(pose.data(), frozen + (field.address - base), sizeof(pose));
        Quaternion checked;
        if (!rotation(pose, checked)) return std::nullopt;
        result.poses.push_back(pose);
    }
    for (const auto field : result.shadow_fields) {
        Vector3 input{}, checked;
        std::memcpy(input.data(), frozen + (field.address - base), field.words * 4);
        if (!interpolate_vector(input, input, 1, field.normal, checked)) return std::nullopt;
        result.shadow_inputs.push_back(input);
    }
    return result;
}

PoseContinuity RacePoses::compare_with(const RacePoses& previous) const {
    PoseContinuity result{previous.identity != identity, previous.poses.size() != poses.size(),
                          previous.shadow_inputs.size() != shadow_inputs.size(),
                          simulation_sample - previous.simulation_sample};
    if (result.reset()) return result;
    // A bounded discontinuity heuristic for single-view races: translations
    // over 64 world units and reversing ground normals reset interpolation.
    for (std::size_t i = 0; i < poses.size(); ++i) {
        double distance_squared = 0;
        for (unsigned j = 12; j < 15; ++j) {
            const double delta = component(poses[i], j) - component(previous.poses[i], j);
            distance_squared += delta * delta;
        }
        if (distance_squared > 64 * 64) {
            result.pose_jump = true;
            return result;
        }
    }
    for (std::size_t i = 0; i < shadow_inputs.size(); ++i) {
        double distance_squared = 0, dot = 0;
        for (unsigned j = 0; j < 3; ++j) {
            const double a = std::bit_cast<float>(previous.shadow_inputs[i][j]);
            const double b = std::bit_cast<float>(shadow_inputs[i][j]);
            distance_squared += (a - b) * (a - b);
            dot += a * b;
        }
        if (shadow_fields[i].normal ? dot <= 0 : distance_squared > 64 * 64) {
            result.shadow_jump = true;
            return result;
        }
    }
    return result;
}

std::optional<PoseChanges> RacePoses::apply(RenderBank& bank, const RacePoses& previous,
                                            double alpha, PoseBlend blend) const {
    if (poses.empty() || bank.empty() || previous.poses.size() != poses.size() ||
        previous.shadow_inputs.size() != shadow_inputs.size() ||
        previous.identity != identity || !std::isfinite(alpha) || alpha < 0 || alpha > 1)
        return std::nullopt;
    PoseChanges changes;
    for (std::size_t i = 0; i < poses.size(); ++i) {
        const auto field = pose_fields[i];
        const bool interpolate = blend == PoseBlend::All ||
                                 (blend == PoseBlend::WheelsOnly && field.kind == PoseKind::Wheel);
        Pose pose;
        if (!interpolate_pose(previous.poses[i], poses[i], interpolate ? alpha : 1.0, pose))
            return std::nullopt;
        if (pose != poses[i]) {
            switch (field.kind) {
                case PoseKind::Camera: ++changes.camera; break;
                case PoseKind::Body: ++changes.bodies; break;
                case PoseKind::Wheel: ++changes.wheels; break;
            }
        }
        std::memcpy(bank.data() + (field.address - 0x80000000), pose.data(), sizeof(pose));
    }
    for (std::size_t i = 0; i < shadow_inputs.size(); ++i) {
        const auto field = shadow_fields[i];
        Vector3 input;
        if (!interpolate_vector(previous.shadow_inputs[i], shadow_inputs[i], alpha, field.normal, input))
            return std::nullopt;
        changes.shadows += input != shadow_inputs[i];
        std::memcpy(bank.data() + (field.address - 0x80000000), input.data(), field.words * 4);
    }
    return changes;
}
}
