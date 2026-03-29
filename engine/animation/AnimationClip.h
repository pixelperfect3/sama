#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/math/Types.h"

namespace engine::animation
{

// One keyframe: timestamp + value.
template <typename T>
struct Keyframe
{
    float time;  // seconds
    T value;
};

// Per-joint channel data. Empty vectors mean the joint is not animated on that channel.
struct JointChannel
{
    uint32_t jointIndex;
    std::vector<Keyframe<math::Vec3>> positions;
    std::vector<Keyframe<math::Quat>> rotations;
    std::vector<Keyframe<math::Vec3>> scales;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;  // total length in seconds
    std::vector<JointChannel> channels;
};

}  // namespace engine::animation
