#pragma once

#include <cstdint>
#include <functional>

#include "engine/animation/IkComponents.h"
#include "engine/animation/Pose.h"
#include "engine/math/Types.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// FootIkHelper -- computes IK targets for foot placement on uneven terrain.
//
// Usage:
//   1. Configure per-foot data (hip, knee, ankle joint indices + footHeight).
//   2. Each frame, call computeFootTarget() with the FK ankle world pos and
//      a raycast function to find the ground.
//   3. Pass the returned IkTarget to IkSystem via IkTargetsComponent.
//   4. Call adjustPelvisHeight() to lower the pelvis so both feet reach.
// ---------------------------------------------------------------------------

struct RaycastResult
{
    bool hit = false;
    math::Vec3 position{0.0f};
    math::Vec3 normal{0, 1, 0};
};

using RaycastFn = std::function<RaycastResult(const math::Vec3& origin, const math::Vec3& direction,
                                              float maxDist)>;

struct FootIkHelper
{
    uint32_t hipJoint;
    uint32_t kneeJoint;
    uint32_t ankleJoint;
    float footHeight = 0.05f;

    // Compute the IK target for one foot by raycasting downward from
    // the FK ankle position.
    static IkTarget computeFootTarget(const math::Vec3& fkAnkleWorldPos, float footHeight,
                                      const RaycastFn& raycast);
};

// Adjust the pelvis (root) height so that the shorter leg still reaches
// the ground. Without this, one foot reaches its target but the other
// is pulled into the air.
void adjustPelvisHeight(Pose& pose, uint32_t pelvisJoint, const math::Vec3& leftFootTarget,
                        const math::Vec3& rightFootTarget, const math::Vec3& leftFkAnkle,
                        const math::Vec3& rightFkAnkle);

}  // namespace engine::animation
