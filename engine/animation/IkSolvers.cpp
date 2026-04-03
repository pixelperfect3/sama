#include "engine/animation/IkSolvers.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::animation
{

namespace
{

// Build a local TRS matrix from a JointPose (same as AnimationSystem).
math::Mat4 trsMatrix(const JointPose& jp)
{
    math::Mat4 m(1.0f);
    m = glm::translate(m, jp.position);
    m *= glm::mat4_cast(jp.rotation);
    m = glm::scale(m, jp.scale);
    return m;
}

// Compute a rotation that rotates vector 'from' to vector 'to'.
// Both must be normalized.
math::Quat rotationBetween(const math::Vec3& from, const math::Vec3& to)
{
    float d = glm::dot(from, to);
    if (d >= 1.0f - 1e-6f)
    {
        return math::Quat{1, 0, 0, 0};  // identity
    }
    if (d <= -1.0f + 1e-6f)
    {
        // 180-degree rotation: pick an arbitrary perpendicular axis
        math::Vec3 axis = glm::cross(math::Vec3{1, 0, 0}, from);
        if (glm::dot(axis, axis) < 1e-6f)
        {
            axis = glm::cross(math::Vec3{0, 1, 0}, from);
        }
        axis = glm::normalize(axis);
        return glm::angleAxis(glm::pi<float>(), axis);
    }
    math::Vec3 c = glm::cross(from, to);
    float s = std::sqrt((1.0f + d) * 2.0f);
    float invS = 1.0f / s;
    return glm::normalize(math::Quat{s * 0.5f, c.x * invS, c.y * invS, c.z * invS});
}

// Extract world-space rotation for a joint given parent chain.
math::Quat getWorldRotation(const Skeleton& skeleton, const Pose& pose, uint32_t jointIdx)
{
    math::Quat worldRot = pose.jointPoses[jointIdx].rotation;
    int32_t parent = skeleton.joints[jointIdx].parentIndex;
    while (parent >= 0)
    {
        worldRot = pose.jointPoses[parent].rotation * worldRot;
        parent = skeleton.joints[parent].parentIndex;
    }
    return worldRot;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// computeWorldPositions
// ---------------------------------------------------------------------------

void computeWorldPositions(const Skeleton& skeleton, const Pose& pose, math::Vec3* worldPositions)
{
    const uint32_t jointCount = skeleton.jointCount();
    // Compute world transforms using parent-first ordering.
    std::vector<math::Mat4> worldTransforms(jointCount, math::Mat4(1.0f));

    for (uint32_t i = 0; i < jointCount; ++i)
    {
        math::Mat4 local = trsMatrix(pose.jointPoses[i]);
        int32_t parent = skeleton.joints[i].parentIndex;
        if (parent >= 0 && static_cast<uint32_t>(parent) < jointCount)
        {
            worldTransforms[i] = worldTransforms[parent] * local;
        }
        else
        {
            worldTransforms[i] = local;
        }
        worldPositions[i] = math::Vec3(worldTransforms[i][3]);
    }
}

// ---------------------------------------------------------------------------
// buildChainFromHierarchy
// ---------------------------------------------------------------------------

memory::InlinedVector<uint32_t, 8> buildChainFromHierarchy(const Skeleton& skeleton,
                                                           uint32_t rootJoint, uint32_t endEffector)
{
    memory::InlinedVector<uint32_t, 8> chain;

    // Walk from end effector to root, collecting indices.
    uint32_t current = endEffector;
    while (current != rootJoint)
    {
        chain.push_back(current);
        int32_t parent = skeleton.joints[current].parentIndex;
        if (parent < 0)
        {
            break;  // reached skeleton root without finding rootJoint
        }
        current = static_cast<uint32_t>(parent);
    }
    chain.push_back(rootJoint);

    // Reverse to get root-to-tip order.
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// ---------------------------------------------------------------------------
// solveTwoBone -- analytical solver using law of cosines
// ---------------------------------------------------------------------------

void solveTwoBone(const Skeleton& skeleton, Pose& pose, math::Vec3* worldPositions,
                  uint32_t rootJoint, uint32_t midJoint, uint32_t tipJoint,
                  const math::Vec3& targetPos, const math::Vec3& poleVector)
{
    math::Vec3 rootPos = worldPositions[rootJoint];
    math::Vec3 midPos = worldPositions[midJoint];
    math::Vec3 tipPos = worldPositions[tipJoint];

    float upperLen = glm::length(midPos - rootPos);  // bone a
    float lowerLen = glm::length(tipPos - midPos);   // bone b

    if (upperLen < 1e-6f || lowerLen < 1e-6f)
    {
        return;  // degenerate chain
    }

    math::Vec3 toTarget = targetPos - rootPos;
    float targetDist = glm::length(toTarget);

    if (targetDist < 1e-6f)
    {
        return;  // target at root -- degenerate
    }

    float a = upperLen;
    float b = lowerLen;
    float maxReach = a + b;

    // Clamp target distance to reachable range.
    float d = glm::clamp(targetDist, std::abs(a - b) + 1e-4f, maxReach - 1e-4f);

    // Law of cosines: angle at root joint
    float cosAngleRoot = (a * a + d * d - b * b) / (2.0f * a * d);
    cosAngleRoot = glm::clamp(cosAngleRoot, -1.0f, 1.0f);
    float angleRoot = std::acos(cosAngleRoot);

    // Law of cosines: angle at mid joint
    float cosAngleMid = (a * a + b * b - d * d) / (2.0f * a * b);
    cosAngleMid = glm::clamp(cosAngleMid, -1.0f, 1.0f);
    float angleMid = std::acos(cosAngleMid);

    // Compute the plane of the solution using pole vector.
    math::Vec3 targetDir = glm::normalize(toTarget);

    // Project pole vector onto plane perpendicular to target direction.
    math::Vec3 poleDir = poleVector - rootPos;
    poleDir = poleDir - glm::dot(poleDir, targetDir) * targetDir;
    float poleDirLen = glm::length(poleDir);
    if (poleDirLen < 1e-6f)
    {
        // Pole vector is along target direction; pick an arbitrary perpendicular.
        poleDir = math::Vec3{0, 1, 0};
        poleDir = poleDir - glm::dot(poleDir, targetDir) * targetDir;
        poleDirLen = glm::length(poleDir);
        if (poleDirLen < 1e-6f)
        {
            poleDir = math::Vec3{0, 0, 1};
            poleDir = poleDir - glm::dot(poleDir, targetDir) * targetDir;
            poleDirLen = glm::length(poleDir);
        }
    }
    poleDir = glm::normalize(poleDir);

    // Compute desired mid joint position.
    // The mid joint lies in the plane defined by (targetDir, poleDir).
    // At angle angleRoot from the target direction, at distance a from root.
    math::Vec3 desiredMidPos =
        rootPos + targetDir * (a * std::cos(angleRoot)) + poleDir * (a * std::sin(angleRoot));

    // Compute desired tip position.
    math::Vec3 desiredTipPos = rootPos + glm::normalize(toTarget) * d;

    // --- Apply rotations ---

    // Root joint: rotate so that the current root->mid direction becomes
    // the desired root->mid direction.
    math::Quat rootWorldRot = getWorldRotation(skeleton, pose, rootJoint);
    math::Quat rootWorldRotInv = glm::inverse(rootWorldRot);

    math::Vec3 currentRootToMid = glm::normalize(midPos - rootPos);
    math::Vec3 desiredRootToMid = glm::normalize(desiredMidPos - rootPos);

    math::Quat rootCorrection = rotationBetween(currentRootToMid, desiredRootToMid);
    // Apply correction in local space.
    math::Quat rootLocalCorrection = rootWorldRotInv * rootCorrection * rootWorldRot;
    pose.jointPoses[rootJoint].rotation = pose.jointPoses[rootJoint].rotation * rootLocalCorrection;

    // Recompute world positions after root rotation.
    computeWorldPositions(skeleton, pose, worldPositions);
    midPos = worldPositions[midJoint];
    tipPos = worldPositions[tipJoint];

    // Mid joint: rotate so that mid->tip direction aligns with mid->target.
    math::Quat midWorldRot = getWorldRotation(skeleton, pose, midJoint);
    math::Quat midWorldRotInv = glm::inverse(midWorldRot);

    math::Vec3 currentMidToTip = tipPos - midPos;
    float midToTipLen = glm::length(currentMidToTip);
    if (midToTipLen < 1e-6f)
    {
        return;
    }
    currentMidToTip = glm::normalize(currentMidToTip);

    math::Vec3 desiredMidToTip = glm::normalize(desiredTipPos - midPos);

    math::Quat midCorrection = rotationBetween(currentMidToTip, desiredMidToTip);
    math::Quat midLocalCorrection = midWorldRotInv * midCorrection * midWorldRot;
    pose.jointPoses[midJoint].rotation = pose.jointPoses[midJoint].rotation * midLocalCorrection;

    // Recompute world positions after mid rotation.
    computeWorldPositions(skeleton, pose, worldPositions);
}

// ---------------------------------------------------------------------------
// solveCcd
// ---------------------------------------------------------------------------

#if ENGINE_IK_ENABLE_CCD
void solveCcd(const Skeleton& skeleton, Pose& pose, math::Vec3* worldPositions,
              const memory::InlinedVector<uint32_t, 8>& chainJoints, const math::Vec3& targetPos,
              uint32_t maxIterations, float tolerance, float dampingFactor)
{
    if (chainJoints.size() < 2)
    {
        return;
    }

    uint32_t endEffector = chainJoints.back();

    for (uint32_t iter = 0; iter < maxIterations; ++iter)
    {
        // Check convergence.
        math::Vec3 effectorPos = worldPositions[endEffector];
        float dist = glm::length(effectorPos - targetPos);
        if (dist < tolerance)
        {
            break;
        }

        // Iterate from tip-1 back to root.
        for (int32_t ji = static_cast<int32_t>(chainJoints.size()) - 2; ji >= 0; --ji)
        {
            uint32_t jointIdx = chainJoints[ji];
            effectorPos = worldPositions[endEffector];
            math::Vec3 jointPos = worldPositions[jointIdx];

            math::Vec3 toEffector = effectorPos - jointPos;
            math::Vec3 toTarget = targetPos - jointPos;

            float effLen = glm::length(toEffector);
            float tarLen = glm::length(toTarget);
            if (effLen < 1e-6f || tarLen < 1e-6f)
            {
                continue;
            }

            toEffector = glm::normalize(toEffector);
            toTarget = glm::normalize(toTarget);

            math::Quat correction = rotationBetween(toEffector, toTarget);

            // Apply damping.
            if (dampingFactor < 1.0f)
            {
                correction = glm::slerp(math::Quat{1, 0, 0, 0}, correction, dampingFactor);
            }

            // Convert to local space and apply.
            math::Quat worldRot = getWorldRotation(skeleton, pose, jointIdx);
            math::Quat worldRotInv = glm::inverse(worldRot);
            math::Quat localCorrection = worldRotInv * correction * worldRot;
            pose.jointPoses[jointIdx].rotation =
                pose.jointPoses[jointIdx].rotation * localCorrection;

            // Recompute world positions for downstream joints.
            computeWorldPositions(skeleton, pose, worldPositions);
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// solveFabrik
// ---------------------------------------------------------------------------

#if ENGINE_IK_ENABLE_FABRIK
void solveFabrik(const Skeleton& skeleton, Pose& pose, math::Vec3* worldPositions,
                 const memory::InlinedVector<uint32_t, 8>& chainJoints, const math::Vec3& targetPos,
                 uint32_t maxIterations, float tolerance)
{
    if (chainJoints.size() < 2)
    {
        return;
    }

    const size_t n = chainJoints.size();

    // Store original world positions for the chain.
    memory::InlinedVector<math::Vec3, 8> positions;
    positions.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        positions[i] = worldPositions[chainJoints[i]];
    }

    // Compute bone lengths.
    memory::InlinedVector<float, 8> boneLengths;
    boneLengths.resize(n - 1);
    for (size_t i = 0; i < n - 1; ++i)
    {
        boneLengths[i] = glm::length(positions[i + 1] - positions[i]);
    }

    math::Vec3 rootOriginal = positions[0];

    for (uint32_t iter = 0; iter < maxIterations; ++iter)
    {
        // Check convergence.
        float dist = glm::length(positions[n - 1] - targetPos);
        if (dist < tolerance)
        {
            break;
        }

        // Forward pass (tip to root): move end effector to target.
        positions[n - 1] = targetPos;
        for (int32_t i = static_cast<int32_t>(n) - 2; i >= 0; --i)
        {
            math::Vec3 dir = positions[i] - positions[i + 1];
            float len = glm::length(dir);
            if (len < 1e-6f)
            {
                dir = math::Vec3{0, 1, 0};
            }
            else
            {
                dir = dir / len;
            }
            positions[i] = positions[i + 1] + dir * boneLengths[i];
        }

        // Backward pass (root to tip): anchor root.
        positions[0] = rootOriginal;
        for (size_t i = 1; i < n; ++i)
        {
            math::Vec3 dir = positions[i] - positions[i - 1];
            float len = glm::length(dir);
            if (len < 1e-6f)
            {
                dir = math::Vec3{0, 1, 0};
            }
            else
            {
                dir = dir / len;
            }
            positions[i] = positions[i - 1] + dir * boneLengths[i - 1];
        }
    }

    // Convert solved positions back to local rotations.
    // For each joint, compute the rotation that transforms the original
    // bone direction to the solved bone direction.
    for (size_t i = 0; i < n - 1; ++i)
    {
        uint32_t jointIdx = chainJoints[i];
        uint32_t childIdx = chainJoints[i + 1];

        // Original bone direction in world space.
        math::Vec3 origDir = worldPositions[childIdx] - worldPositions[jointIdx];
        float origLen = glm::length(origDir);
        if (origLen < 1e-6f)
        {
            continue;
        }
        origDir = glm::normalize(origDir);

        // Solved bone direction.
        math::Vec3 solvedDir = positions[i + 1] - positions[i];
        float solvedLen = glm::length(solvedDir);
        if (solvedLen < 1e-6f)
        {
            continue;
        }
        solvedDir = glm::normalize(solvedDir);

        // Compute the rotation that transforms origDir to solvedDir.
        math::Quat correction = rotationBetween(origDir, solvedDir);

        // Convert to local space.
        math::Quat worldRot = getWorldRotation(skeleton, pose, jointIdx);
        math::Quat worldRotInv = glm::inverse(worldRot);
        math::Quat localCorrection = worldRotInv * correction * worldRot;
        pose.jointPoses[jointIdx].rotation = pose.jointPoses[jointIdx].rotation * localCorrection;

        // Recompute world positions for all joints after this one.
        computeWorldPositions(skeleton, pose, worldPositions);
    }
}
#endif

}  // namespace engine::animation
