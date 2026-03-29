#pragma once

#include "engine/math/Types.h"
#include "engine/memory/InlinedVector.h"

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
    memory::InlinedVector<JointPose, 128> jointPoses;  // parallel to Skeleton::joints
};

}  // namespace engine::animation
