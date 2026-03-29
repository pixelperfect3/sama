#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/animation/AnimationClip.h"
#include "engine/animation/AnimationSampler.h"
#include "engine/animation/Skeleton.h"

using namespace engine::animation;
using namespace engine::math;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr float kEps = 1e-4f;

static bool approxEq(float a, float b, float eps = kEps)
{
    return std::abs(a - b) < eps;
}

static bool approxVec3(const Vec3& a, const Vec3& b, float eps = kEps)
{
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps) && approxEq(a.z, b.z, eps);
}

static bool approxQuat(const Quat& a, const Quat& b, float eps = kEps)
{
    // Quaternions q and -q represent the same rotation.
    float dot = glm::dot(a, b);
    return std::abs(std::abs(dot) - 1.0f) < eps;
}

// Build a one-joint skeleton.
static Skeleton make1JointSkeleton()
{
    Skeleton skel;
    skel.joints.resize(1);
    skel.joints[0].parentIndex = -1;
    skel.joints[0].nameHash = 1;
    return skel;
}

// Build a clip with 3 position keyframes for joint 0.
static AnimationClip makePositionClip()
{
    AnimationClip clip;
    clip.name = "test_pos";
    clip.duration = 1.0f;

    JointChannel ch;
    ch.jointIndex = 0;
    ch.positions = {
        {0.0f, Vec3{0.0f, 0.0f, 0.0f}},
        {0.5f, Vec3{5.0f, 0.0f, 0.0f}},
        {1.0f, Vec3{10.0f, 0.0f, 0.0f}},
    };
    clip.channels.push_back(ch);

    return clip;
}

// Build a clip with 2 rotation keyframes: identity at t=0, 90 deg around Y at t=1.
static AnimationClip makeRotationClip()
{
    AnimationClip clip;
    clip.name = "test_rot";
    clip.duration = 1.0f;

    JointChannel ch;
    ch.jointIndex = 0;
    ch.rotations = {
        {0.0f, Quat{1.0f, 0.0f, 0.0f, 0.0f}},
        {1.0f, glm::angleAxis(glm::radians(90.0f), Vec3{0.0f, 1.0f, 0.0f})},
    };
    clip.channels.push_back(ch);

    return clip;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("sampleClip at t=0: exact first keyframe", "[animation]")
{
    Skeleton skel = make1JointSkeleton();
    AnimationClip clip = makePositionClip();
    Pose pose;

    sampleClip(clip, skel, 0.0f, pose);
    REQUIRE(pose.jointPoses.size() == 1);
    CHECK(approxVec3(pose.jointPoses[0].position, Vec3{0.0f, 0.0f, 0.0f}));
}

TEST_CASE("sampleClip at t=1.0: exact last keyframe", "[animation]")
{
    Skeleton skel = make1JointSkeleton();
    AnimationClip clip = makePositionClip();
    Pose pose;

    sampleClip(clip, skel, 1.0f, pose);
    REQUIRE(pose.jointPoses.size() == 1);
    CHECK(approxVec3(pose.jointPoses[0].position, Vec3{10.0f, 0.0f, 0.0f}));
}

TEST_CASE("sampleClip at t=0.25: interpolated between first and second", "[animation]")
{
    Skeleton skel = make1JointSkeleton();
    AnimationClip clip = makePositionClip();
    Pose pose;

    sampleClip(clip, skel, 0.25f, pose);
    REQUIRE(pose.jointPoses.size() == 1);
    // t=0.25 is halfway between kf0 (t=0, pos=0) and kf1 (t=0.5, pos=5).
    CHECK(approxVec3(pose.jointPoses[0].position, Vec3{2.5f, 0.0f, 0.0f}));
}

TEST_CASE("sampleClip at t=-0.1: clamped to first keyframe", "[animation]")
{
    Skeleton skel = make1JointSkeleton();
    AnimationClip clip = makePositionClip();
    Pose pose;

    sampleClip(clip, skel, -0.1f, pose);
    REQUIRE(pose.jointPoses.size() == 1);
    CHECK(approxVec3(pose.jointPoses[0].position, Vec3{0.0f, 0.0f, 0.0f}));
}

TEST_CASE("sampleClip at t=1.5: clamped to last keyframe", "[animation]")
{
    Skeleton skel = make1JointSkeleton();
    AnimationClip clip = makePositionClip();
    Pose pose;

    sampleClip(clip, skel, 1.5f, pose);
    REQUIRE(pose.jointPoses.size() == 1);
    CHECK(approxVec3(pose.jointPoses[0].position, Vec3{10.0f, 0.0f, 0.0f}));
}

TEST_CASE("sampleClip rotation uses slerp at midpoint", "[animation]")
{
    Skeleton skel = make1JointSkeleton();
    AnimationClip clip = makeRotationClip();
    Pose pose;

    sampleClip(clip, skel, 0.5f, pose);
    REQUIRE(pose.jointPoses.size() == 1);

    // Expected: slerp between identity and 90-deg-Y at t=0.5 => 45-deg-Y.
    Quat expected = glm::angleAxis(glm::radians(45.0f), Vec3{0.0f, 1.0f, 0.0f});
    float dot = glm::dot(pose.jointPoses[0].rotation, expected);
    CHECK(std::abs(std::abs(dot) - 1.0f) < 1e-3f);
}
