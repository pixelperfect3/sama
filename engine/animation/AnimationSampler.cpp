#include "engine/animation/AnimationSampler.h"

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::animation
{

namespace
{

// Binary search for the keyframe pair enclosing 'time'.
// Returns the interpolation factor t in [0,1] and the indices of the two
// keyframes to interpolate between.
template <typename T>
T sampleChannel(const std::vector<Keyframe<T>>& keyframes, float time, const T& defaultValue)
{
    if (keyframes.empty())
        return defaultValue;

    if (keyframes.size() == 1 || time <= keyframes.front().time)
        return keyframes.front().value;

    if (time >= keyframes.back().time)
        return keyframes.back().value;

    // Binary search: find first keyframe with time > 'time'.
    auto it = std::upper_bound(
        keyframes.begin(), keyframes.end(), time,
        [](float t, const Keyframe<T>& kf) { return t < kf.time; });

    // it points to the keyframe after 'time'; (it - 1) is the one before.
    const auto& kf1 = *(it - 1);
    const auto& kf2 = *it;

    const float dt = kf2.time - kf1.time;
    const float t = (dt > 0.0f) ? (time - kf1.time) / dt : 0.0f;

    return glm::mix(kf1.value, kf2.value, t);
}

// Specialization for quaternion: use slerp instead of mix.
template <>
math::Quat sampleChannel<math::Quat>(const std::vector<Keyframe<math::Quat>>& keyframes,
                                      float time, const math::Quat& defaultValue)
{
    if (keyframes.empty())
        return defaultValue;

    if (keyframes.size() == 1 || time <= keyframes.front().time)
        return keyframes.front().value;

    if (time >= keyframes.back().time)
        return keyframes.back().value;

    auto it = std::upper_bound(
        keyframes.begin(), keyframes.end(), time,
        [](float t, const Keyframe<math::Quat>& kf) { return t < kf.time; });

    const auto& kf1 = *(it - 1);
    const auto& kf2 = *it;

    const float dt = kf2.time - kf1.time;
    const float t = (dt > 0.0f) ? (time - kf1.time) / dt : 0.0f;

    return glm::slerp(kf1.value, kf2.value, t);
}

}  // anonymous namespace

void sampleClip(const AnimationClip& clip, const Skeleton& skeleton, float time, Pose& outPose)
{
    const uint32_t jointCount = skeleton.jointCount();
    outPose.jointPoses.resize(jointCount);

    // Initialize to default (identity) pose.
    for (uint32_t i = 0; i < jointCount; ++i)
    {
        outPose.jointPoses[i].position = math::Vec3(0.0f);
        outPose.jointPoses[i].rotation = math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        outPose.jointPoses[i].scale = math::Vec3(1.0f);
    }

    // Sample each channel.
    for (const auto& channel : clip.channels)
    {
        if (channel.jointIndex >= jointCount)
            continue;

        auto& jp = outPose.jointPoses[channel.jointIndex];
        jp.position = sampleChannel(channel.positions, time, jp.position);
        jp.rotation = sampleChannel(channel.rotations, time, jp.rotation);
        jp.scale = sampleChannel(channel.scales, time, jp.scale);
    }
}

void blendPoses(const Pose& a, const Pose& b, float t, Pose& outPose)
{
    const size_t count = a.jointPoses.size();
    outPose.jointPoses.resize(count);

    for (size_t i = 0; i < count; ++i)
    {
        outPose.jointPoses[i].position =
            glm::mix(a.jointPoses[i].position, b.jointPoses[i].position, t);
        outPose.jointPoses[i].rotation =
            glm::slerp(a.jointPoses[i].rotation, b.jointPoses[i].rotation, t);
        outPose.jointPoses[i].scale =
            glm::mix(a.jointPoses[i].scale, b.jointPoses[i].scale, t);
    }
}

}  // namespace engine::animation
