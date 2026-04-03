#include "engine/animation/FootIkHelper.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::animation
{

IkTarget FootIkHelper::computeFootTarget(const math::Vec3& fkAnkleWorldPos, float footHeight,
                                         const RaycastFn& raycast)
{
    IkTarget target;
    target.position = fkAnkleWorldPos;

    // Cast ray downward from ankle.
    RaycastResult result =
        raycast(fkAnkleWorldPos + math::Vec3{0, 0.5f, 0}, math::Vec3{0, -1, 0}, 2.0f);

    if (result.hit)
    {
        // Place foot at ground hit point + offset along normal.
        target.position = result.position + result.normal * footHeight;

        // Orient foot so sole is parallel to ground surface.
        // Compute rotation from up vector to surface normal.
        math::Vec3 up{0, 1, 0};
        float d = glm::dot(up, result.normal);
        if (d < 1.0f - 1e-6f)
        {
            math::Vec3 axis = glm::normalize(glm::cross(up, result.normal));
            float angle = std::acos(glm::clamp(d, -1.0f, 1.0f));
            target.orientation = glm::angleAxis(angle, axis);
        }
        else
        {
            target.orientation = math::Quat{1, 0, 0, 0};
        }
        target.hasOrientation = 1;
    }

    return target;
}

void adjustPelvisHeight(Pose& pose, uint32_t pelvisJoint, const math::Vec3& leftFootTarget,
                        const math::Vec3& rightFootTarget, const math::Vec3& leftFkAnkle,
                        const math::Vec3& rightFkAnkle)
{
    // Compute how much each foot needs to move down from FK position.
    float leftOffset = leftFkAnkle.y - leftFootTarget.y;
    float rightOffset = rightFkAnkle.y - rightFootTarget.y;

    // Lower pelvis by the larger offset (so both feet can reach).
    float pelvisAdjust = std::max(leftOffset, rightOffset);
    if (pelvisAdjust > 0.0f)
    {
        pose.jointPoses[pelvisJoint].position.y -= pelvisAdjust;
    }
}

}  // namespace engine::animation
