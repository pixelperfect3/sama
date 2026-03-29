#pragma once

#include "engine/animation/AnimationClip.h"
#include "engine/animation/Pose.h"
#include "engine/animation/Skeleton.h"

namespace engine::animation
{

// ---------------------------------------------------------------------------
// sampleClip -- evaluate an AnimationClip at a given time into a Pose.
//
// Uses binary search (std::upper_bound) for keyframe lookup, then:
//   position/scale: glm::mix (linear interpolation)
//   rotation:       glm::slerp (spherical linear interpolation)
//
// Joints not present in the clip retain their default pose (identity TRS).
// ---------------------------------------------------------------------------
void sampleClip(const AnimationClip& clip, const Skeleton& skeleton, float time, Pose& outPose);

// ---------------------------------------------------------------------------
// blendPoses -- blend two poses per-joint.
//
//   position/scale: glm::mix(a, b, t)
//   rotation:       glm::slerp(a, b, t)
//
// Both poses must have the same number of joint poses (matching the skeleton).
// ---------------------------------------------------------------------------
void blendPoses(const Pose& a, const Pose& b, float t, Pose& outPose);

}  // namespace engine::animation
