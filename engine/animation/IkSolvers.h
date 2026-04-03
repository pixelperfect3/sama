#pragma once

#include <cstdint>

#include "engine/animation/Pose.h"
#include "engine/animation/Skeleton.h"
#include "engine/math/Types.h"
#include "engine/memory/InlinedVector.h"

#ifndef ENGINE_IK_ENABLE_CCD
#define ENGINE_IK_ENABLE_CCD 1
#endif

#ifndef ENGINE_IK_ENABLE_FABRIK
#define ENGINE_IK_ENABLE_FABRIK 1
#endif

namespace engine::animation
{

// ---------------------------------------------------------------------------
// Free-function IK solvers.
//
// All solvers operate on a Pose (array of local-space JointPose) and a
// parallel array of world-space joint positions. They modify the Pose in
// place and update world positions as needed.
// ---------------------------------------------------------------------------

// Compute world-space positions for all joints from the current pose.
// worldPositions must be pre-sized to skeleton.jointCount().
void computeWorldPositions(const Skeleton& skeleton, const Pose& pose, math::Vec3* worldPositions);

// Build a chain of joint indices from endEffector up to (and including)
// rootJoint by walking the skeleton hierarchy. Returns indices ordered
// root-to-tip.
memory::InlinedVector<uint32_t, 8> buildChainFromHierarchy(const Skeleton& skeleton,
                                                           uint32_t rootJoint,
                                                           uint32_t endEffector);

// Two-Bone IK -- analytical solver using law of cosines.
// Modifies rotations of rootJoint and midJoint to place tipJoint at target.
void solveTwoBone(const Skeleton& skeleton, Pose& pose, math::Vec3* worldPositions,
                  uint32_t rootJoint, uint32_t midJoint, uint32_t tipJoint,
                  const math::Vec3& targetPos, const math::Vec3& poleVector);

#if ENGINE_IK_ENABLE_CCD
// CCD (Cyclic Coordinate Descent) -- iterative solver.
// chainJoints ordered root-to-tip; last element is the end effector.
void solveCcd(const Skeleton& skeleton, Pose& pose, math::Vec3* worldPositions,
              const memory::InlinedVector<uint32_t, 8>& chainJoints, const math::Vec3& targetPos,
              uint32_t maxIterations, float tolerance = 0.001f, float dampingFactor = 1.0f);
#endif

#if ENGINE_IK_ENABLE_FABRIK
// FABRIK (Forward And Backward Reaching Inverse Kinematics).
// chainJoints ordered root-to-tip; last element is the end effector.
void solveFabrik(const Skeleton& skeleton, Pose& pose, math::Vec3* worldPositions,
                 const memory::InlinedVector<uint32_t, 8>& chainJoints, const math::Vec3& targetPos,
                 uint32_t maxIterations, float tolerance = 0.001f);
#endif

}  // namespace engine::animation
