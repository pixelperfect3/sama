#pragma once

#include <vector>

#include "engine/math/Types.h"

namespace engine::animation
{

struct JointPose
{
    math::Vec3 position{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity
    math::Vec3 scale{1.0f};
};

struct Pose
{
    std::vector<JointPose> jointPoses;  // parallel to Skeleton::joints
};

}  // namespace engine::animation
