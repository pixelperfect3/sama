#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/animation/AnimationSampler.h"
#include "engine/animation/Pose.h"

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

// Build a 2-joint pose with given positions and rotations.
static Pose makePose(Vec3 pos0, Quat rot0, Vec3 pos1, Quat rot1)
{
    Pose p;
    p.jointPoses.resize(2);
    p.jointPoses[0].position = pos0;
    p.jointPoses[0].rotation = rot0;
    p.jointPoses[0].scale = Vec3{1.0f};
    p.jointPoses[1].position = pos1;
    p.jointPoses[1].rotation = rot1;
    p.jointPoses[1].scale = Vec3{1.0f};
    return p;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("blendPoses at t=0 equals pose A", "[animation]")
{
    Quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    Quat rot90Y = glm::angleAxis(glm::radians(90.0f), Vec3{0.0f, 1.0f, 0.0f});

    Pose a = makePose(Vec3{1.0f, 2.0f, 3.0f}, identity, Vec3{4.0f, 5.0f, 6.0f}, identity);
    Pose b = makePose(Vec3{10.0f, 20.0f, 30.0f}, rot90Y, Vec3{40.0f, 50.0f, 60.0f}, rot90Y);
    Pose out;

    blendPoses(a, b, 0.0f, out);

    REQUIRE(out.jointPoses.size() == 2);
    CHECK(approxVec3(out.jointPoses[0].position, Vec3{1.0f, 2.0f, 3.0f}));
    CHECK(approxVec3(out.jointPoses[1].position, Vec3{4.0f, 5.0f, 6.0f}));

    float dot0 = glm::dot(out.jointPoses[0].rotation, identity);
    CHECK(std::abs(std::abs(dot0) - 1.0f) < kEps);
}

TEST_CASE("blendPoses at t=1 equals pose B", "[animation]")
{
    Quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    Quat rot90Y = glm::angleAxis(glm::radians(90.0f), Vec3{0.0f, 1.0f, 0.0f});

    Pose a = makePose(Vec3{1.0f, 2.0f, 3.0f}, identity, Vec3{4.0f, 5.0f, 6.0f}, identity);
    Pose b = makePose(Vec3{10.0f, 20.0f, 30.0f}, rot90Y, Vec3{40.0f, 50.0f, 60.0f}, rot90Y);
    Pose out;

    blendPoses(a, b, 1.0f, out);

    REQUIRE(out.jointPoses.size() == 2);
    CHECK(approxVec3(out.jointPoses[0].position, Vec3{10.0f, 20.0f, 30.0f}));
    CHECK(approxVec3(out.jointPoses[1].position, Vec3{40.0f, 50.0f, 60.0f}));

    float dot0 = glm::dot(out.jointPoses[0].rotation, rot90Y);
    CHECK(std::abs(std::abs(dot0) - 1.0f) < kEps);
}

TEST_CASE("blendPoses at t=0.5: midpoint position and slerp rotation", "[animation]")
{
    Quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    Quat rot90Y = glm::angleAxis(glm::radians(90.0f), Vec3{0.0f, 1.0f, 0.0f});

    Pose a = makePose(Vec3{0.0f, 0.0f, 0.0f}, identity, Vec3{10.0f, 0.0f, 0.0f}, identity);
    Pose b = makePose(Vec3{10.0f, 0.0f, 0.0f}, rot90Y, Vec3{20.0f, 0.0f, 0.0f}, rot90Y);
    Pose out;

    blendPoses(a, b, 0.5f, out);

    REQUIRE(out.jointPoses.size() == 2);

    // Position: midpoint.
    CHECK(approxVec3(out.jointPoses[0].position, Vec3{5.0f, 0.0f, 0.0f}));
    CHECK(approxVec3(out.jointPoses[1].position, Vec3{15.0f, 0.0f, 0.0f}));

    // Rotation: slerp midpoint = 45-deg around Y.
    Quat expected45Y = glm::angleAxis(glm::radians(45.0f), Vec3{0.0f, 1.0f, 0.0f});
    float dot0 = glm::dot(out.jointPoses[0].rotation, expected45Y);
    CHECK(std::abs(std::abs(dot0) - 1.0f) < 1e-3f);
    float dot1 = glm::dot(out.jointPoses[1].rotation, expected45Y);
    CHECK(std::abs(std::abs(dot1) - 1.0f) < 1e-3f);
}
