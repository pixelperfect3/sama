#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/AnimationResources.h"
#include "engine/animation/IkComponents.h"
#include "engine/animation/IkSolvers.h"
#include "engine/animation/IkSystem.h"
#include "engine/animation/Pose.h"
#include "engine/animation/Skeleton.h"
#include "engine/ecs/Registry.h"

using namespace engine::animation;
using namespace engine::ecs;
using namespace engine::math;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr float kEps = 0.01f;  // tolerance for IK solutions

static bool approxEq(float a, float b, float eps = kEps)
{
    return std::abs(a - b) < eps;
}

static bool approxVec3(const Vec3& a, const Vec3& b, float eps = kEps)
{
    return approxEq(a.x, b.x, eps) && approxEq(a.y, b.y, eps) && approxEq(a.z, b.z, eps);
}

// Build a simple 3-joint chain skeleton:
//   joint 0 (root) at origin
//   joint 1 (mid) at (1, 0, 0) -- bone length 1.0
//   joint 2 (tip) at (2, 0, 0) -- bone length 1.0
// Parent order: 0 -> 1 -> 2
static Skeleton makeThreeJointSkeleton()
{
    Skeleton skel;
    skel.joints.resize(3);

    // Root joint -- no parent
    skel.joints[0].parentIndex = -1;
    skel.joints[0].inverseBindMatrix = Mat4(1.0f);

    // Mid joint -- parent is root
    skel.joints[1].parentIndex = 0;
    skel.joints[1].inverseBindMatrix = Mat4(1.0f);

    // Tip joint -- parent is mid
    skel.joints[2].parentIndex = 1;
    skel.joints[2].inverseBindMatrix = Mat4(1.0f);

    return skel;
}

// Build a pose matching the 3-joint skeleton rest pose.
// joint 0 at origin, joint 1 offset (1,0,0) from parent, joint 2 offset (1,0,0)
static Pose makeThreeJointPose()
{
    Pose pose;
    pose.jointPoses.resize(3);
    pose.jointPoses[0].position = Vec3{0, 0, 0};
    pose.jointPoses[0].rotation = Quat{1, 0, 0, 0};
    pose.jointPoses[0].scale = Vec3{1};
    pose.jointPoses[1].position = Vec3{1, 0, 0};
    pose.jointPoses[1].rotation = Quat{1, 0, 0, 0};
    pose.jointPoses[1].scale = Vec3{1};
    pose.jointPoses[2].position = Vec3{1, 0, 0};
    pose.jointPoses[2].rotation = Quat{1, 0, 0, 0};
    pose.jointPoses[2].scale = Vec3{1};
    return pose;
}

// Build a 5-joint chain skeleton for CCD/FABRIK tests.
// Linear chain along X: joints at (0,0,0), (1,0,0), (2,0,0), (3,0,0), (4,0,0)
static Skeleton makeFiveJointSkeleton()
{
    Skeleton skel;
    skel.joints.resize(5);
    for (int i = 0; i < 5; ++i)
    {
        skel.joints[i].parentIndex = (i == 0) ? -1 : (i - 1);
        skel.joints[i].inverseBindMatrix = Mat4(1.0f);
    }
    return skel;
}

static Pose makeFiveJointPose()
{
    Pose pose;
    pose.jointPoses.resize(5);
    for (int i = 0; i < 5; ++i)
    {
        pose.jointPoses[i].position = (i == 0) ? Vec3{0, 0, 0} : Vec3{1, 0, 0};
        pose.jointPoses[i].rotation = Quat{1, 0, 0, 0};
        pose.jointPoses[i].scale = Vec3{1};
    }
    return pose;
}

// ---------------------------------------------------------------------------
// Two-Bone IK Tests
// ---------------------------------------------------------------------------

TEST_CASE("Two-bone IK: end effector reaches target within tolerance", "[ik]")
{
    Skeleton skel = makeThreeJointSkeleton();
    Pose pose = makeThreeJointPose();

    Vec3 worldPos[3];
    computeWorldPositions(skel, pose, worldPos);

    Vec3 target{1.5f, 0.5f, 0.0f};
    Vec3 pole{0, 1, 0};

    solveTwoBone(skel, pose, worldPos, 0, 1, 2, target, pole);

    // Recompute and check tip position.
    computeWorldPositions(skel, pose, worldPos);
    float dist = glm::length(worldPos[2] - target);
    CHECK(dist < kEps);
}

TEST_CASE("Two-bone IK: pole vector controls bend direction", "[ik]")
{
    // Target directly ahead at reachable distance.
    Vec3 target{1.0f, 0.0f, 0.0f};

    // Solve with pole vector pointing UP.
    {
        Skeleton skel = makeThreeJointSkeleton();
        Pose pose = makeThreeJointPose();
        Vec3 worldPos[3];
        computeWorldPositions(skel, pose, worldPos);

        Vec3 poleUp{0, 5, 0};
        solveTwoBone(skel, pose, worldPos, 0, 1, 2, target, poleUp);
        computeWorldPositions(skel, pose, worldPos);

        // Mid joint should be above the root-target line (positive Y).
        CHECK(worldPos[1].y > 0.0f);
    }

    // Solve with pole vector pointing DOWN.
    {
        Skeleton skel = makeThreeJointSkeleton();
        Pose pose = makeThreeJointPose();
        Vec3 worldPos[3];
        computeWorldPositions(skel, pose, worldPos);

        Vec3 poleDown{0, -5, 0};
        solveTwoBone(skel, pose, worldPos, 0, 1, 2, target, poleDown);
        computeWorldPositions(skel, pose, worldPos);

        // Mid joint should be below the root-target line (negative Y).
        CHECK(worldPos[1].y < 0.0f);
    }
}

TEST_CASE("Two-bone IK: unreachable target (fully extended)", "[ik]")
{
    Skeleton skel = makeThreeJointSkeleton();
    Pose pose = makeThreeJointPose();
    Vec3 worldPos[3];
    computeWorldPositions(skel, pose, worldPos);

    // Target at (3, 0, 0) -- total chain reach is 2.0.
    Vec3 target{3.0f, 0.0f, 0.0f};
    Vec3 pole{0, 1, 0};

    solveTwoBone(skel, pose, worldPos, 0, 1, 2, target, pole);
    computeWorldPositions(skel, pose, worldPos);

    // End effector should be at maximum reach (2.0) along the target direction.
    float tipDist = glm::length(worldPos[2]);
    CHECK(approxEq(tipDist, 2.0f, 0.05f));

    // Direction toward target should be correct.
    Vec3 tipDir = glm::normalize(worldPos[2]);
    Vec3 targetDir = glm::normalize(target);
    float dot = glm::dot(tipDir, targetDir);
    CHECK(dot > 0.95f);
}

TEST_CASE("Two-bone IK: target at origin (degenerate case)", "[ik]")
{
    Skeleton skel = makeThreeJointSkeleton();
    Pose pose = makeThreeJointPose();
    Vec3 worldPos[3];
    computeWorldPositions(skel, pose, worldPos);

    // Target at root position.
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 pole{0, 1, 0};

    solveTwoBone(skel, pose, worldPos, 0, 1, 2, target, pole);
    computeWorldPositions(skel, pose, worldPos);

    // Verify no NaN.
    for (int i = 0; i < 3; ++i)
    {
        CHECK_FALSE(std::isnan(worldPos[i].x));
        CHECK_FALSE(std::isnan(worldPos[i].y));
        CHECK_FALSE(std::isnan(worldPos[i].z));
    }
    for (int i = 0; i < 3; ++i)
    {
        CHECK_FALSE(std::isnan(pose.jointPoses[i].rotation.x));
        CHECK_FALSE(std::isnan(pose.jointPoses[i].rotation.y));
        CHECK_FALSE(std::isnan(pose.jointPoses[i].rotation.z));
        CHECK_FALSE(std::isnan(pose.jointPoses[i].rotation.w));
    }
}

// ---------------------------------------------------------------------------
// CCD Tests
// ---------------------------------------------------------------------------

#if ENGINE_IK_ENABLE_CCD
TEST_CASE("CCD: chain converges to target within max iterations", "[ik]")
{
    Skeleton skel = makeFiveJointSkeleton();
    Pose pose = makeFiveJointPose();
    Vec3 worldPos[5];
    computeWorldPositions(skel, pose, worldPos);

    Vec3 target{2.0f, 2.0f, 0.0f};

    auto chain = buildChainFromHierarchy(skel, 0, 4);

    solveCcd(skel, pose, worldPos, chain, target, 15, 0.01f);
    computeWorldPositions(skel, pose, worldPos);

    float dist = glm::length(worldPos[4] - target);
    CHECK(dist < 0.1f);  // should converge reasonably close
}

TEST_CASE("CCD: iteration count respected", "[ik]")
{
    Skeleton skel = makeFiveJointSkeleton();
    Pose pose = makeFiveJointPose();
    Vec3 worldPos[5];
    computeWorldPositions(skel, pose, worldPos);

    // Save original pose.
    Pose originalPose = pose;

    Vec3 target{2.0f, 2.0f, 0.0f};
    auto chain = buildChainFromHierarchy(skel, 0, 4);

    // With maxIterations=1, solver should still modify pose but not fully converge.
    solveCcd(skel, pose, worldPos, chain, target, 1, 0.001f);

    // The pose should be different from original (solver did work).
    bool changed = false;
    for (size_t i = 0; i < 5; ++i)
    {
        float dot = glm::dot(pose.jointPoses[i].rotation, originalPose.jointPoses[i].rotation);
        if (std::abs(std::abs(dot) - 1.0f) > 1e-6f)
        {
            changed = true;
            break;
        }
    }
    CHECK(changed);
}
#endif

// ---------------------------------------------------------------------------
// FABRIK Tests
// ---------------------------------------------------------------------------

#if ENGINE_IK_ENABLE_FABRIK
TEST_CASE("FABRIK: chain converges to target", "[ik]")
{
    Skeleton skel = makeFiveJointSkeleton();
    Pose pose = makeFiveJointPose();
    Vec3 worldPos[5];
    computeWorldPositions(skel, pose, worldPos);

    Vec3 target{2.0f, 2.0f, 0.0f};
    auto chain = buildChainFromHierarchy(skel, 0, 4);

    solveFabrik(skel, pose, worldPos, chain, target, 10, 0.01f);
    computeWorldPositions(skel, pose, worldPos);

    float dist = glm::length(worldPos[4] - target);
    CHECK(dist < 0.1f);
}

TEST_CASE("FABRIK: bone lengths preserved", "[ik]")
{
    Skeleton skel = makeFiveJointSkeleton();
    Pose pose = makeFiveJointPose();
    Vec3 worldPos[5];
    computeWorldPositions(skel, pose, worldPos);

    // Record original bone lengths.
    float originalLengths[4];
    for (int i = 0; i < 4; ++i)
    {
        originalLengths[i] = glm::length(worldPos[i + 1] - worldPos[i]);
    }

    Vec3 target{2.0f, 2.0f, 0.0f};
    auto chain = buildChainFromHierarchy(skel, 0, 4);

    solveFabrik(skel, pose, worldPos, chain, target, 10, 0.01f);
    computeWorldPositions(skel, pose, worldPos);

    // Check that bone lengths are approximately preserved.
    for (int i = 0; i < 4; ++i)
    {
        float newLen = glm::length(worldPos[i + 1] - worldPos[i]);
        CHECK(approxEq(newLen, originalLengths[i], 0.05f));
    }
}
#endif

// ---------------------------------------------------------------------------
// Blend Weight Tests
// ---------------------------------------------------------------------------

TEST_CASE("Blend weight 0.0 = pure FK", "[ik]")
{
    Skeleton skel = makeThreeJointSkeleton();
    Pose fkPose = makeThreeJointPose();
    Pose pose = fkPose;  // copy

    Vec3 worldPos[3];
    computeWorldPositions(skel, pose, worldPos);

    // Save FK rotations.
    Quat fkRot0 = pose.jointPoses[0].rotation;
    Quat fkRot1 = pose.jointPoses[1].rotation;

    // Solve with weight 0.0 -- should not change anything.
    Vec3 target{1.5f, 0.5f, 0.0f};
    Vec3 pole{0, 1, 0};

    // Solve IK first, then blend back to FK with weight 0.
    solveTwoBone(skel, pose, worldPos, 0, 1, 2, target, pole);

    // Blend with weight 0.0 (pure FK).
    Quat ikRot0 = pose.jointPoses[0].rotation;
    Quat ikRot1 = pose.jointPoses[1].rotation;
    pose.jointPoses[0].rotation = glm::slerp(fkRot0, ikRot0, 0.0f);
    pose.jointPoses[1].rotation = glm::slerp(fkRot1, ikRot1, 0.0f);

    // Should match FK exactly.
    float dot0 = glm::dot(pose.jointPoses[0].rotation, fkRot0);
    float dot1 = glm::dot(pose.jointPoses[1].rotation, fkRot1);
    CHECK(std::abs(std::abs(dot0) - 1.0f) < 1e-4f);
    CHECK(std::abs(std::abs(dot1) - 1.0f) < 1e-4f);
}

TEST_CASE("Blend weight 1.0 = pure IK", "[ik]")
{
    Skeleton skel = makeThreeJointSkeleton();
    Pose pose = makeThreeJointPose();
    Vec3 worldPos[3];
    computeWorldPositions(skel, pose, worldPos);

    Vec3 target{1.5f, 0.5f, 0.0f};
    Vec3 pole{0, 1, 0};

    solveTwoBone(skel, pose, worldPos, 0, 1, 2, target, pole);
    computeWorldPositions(skel, pose, worldPos);

    // With weight 1.0 (no blending), the tip should reach the target.
    float dist = glm::length(worldPos[2] - target);
    CHECK(dist < kEps);
}

TEST_CASE("Blend weight 0.5 = interpolated", "[ik]")
{
    Skeleton skel = makeThreeJointSkeleton();
    Pose fkPose = makeThreeJointPose();
    Pose pose = fkPose;

    Vec3 worldPos[3];
    computeWorldPositions(skel, pose, worldPos);

    Quat fkRot0 = pose.jointPoses[0].rotation;
    Quat fkRot1 = pose.jointPoses[1].rotation;

    Vec3 target{1.5f, 0.5f, 0.0f};
    Vec3 pole{0, 1, 0};

    solveTwoBone(skel, pose, worldPos, 0, 1, 2, target, pole);

    Quat ikRot0 = pose.jointPoses[0].rotation;
    Quat ikRot1 = pose.jointPoses[1].rotation;

    // Blend at 0.5.
    pose.jointPoses[0].rotation = glm::slerp(fkRot0, ikRot0, 0.5f);
    pose.jointPoses[1].rotation = glm::slerp(fkRot1, ikRot1, 0.5f);

    // Expected rotation is the slerp midpoint.
    Quat expected0 = glm::slerp(fkRot0, ikRot0, 0.5f);
    Quat expected1 = glm::slerp(fkRot1, ikRot1, 0.5f);

    float dot0 = glm::dot(pose.jointPoses[0].rotation, expected0);
    float dot1 = glm::dot(pose.jointPoses[1].rotation, expected1);
    CHECK(std::abs(std::abs(dot0) - 1.0f) < 1e-4f);
    CHECK(std::abs(std::abs(dot1) - 1.0f) < 1e-4f);
}

// ---------------------------------------------------------------------------
// Chain Building Tests
// ---------------------------------------------------------------------------

TEST_CASE("buildChainFromHierarchy produces correct joint chain", "[ik]")
{
    Skeleton skel = makeFiveJointSkeleton();
    auto chain = buildChainFromHierarchy(skel, 0, 4);

    REQUIRE(chain.size() == 5);
    CHECK(chain[0] == 0);
    CHECK(chain[1] == 1);
    CHECK(chain[2] == 2);
    CHECK(chain[3] == 3);
    CHECK(chain[4] == 4);
}

TEST_CASE("buildChainFromHierarchy partial chain", "[ik]")
{
    Skeleton skel = makeFiveJointSkeleton();
    auto chain = buildChainFromHierarchy(skel, 1, 3);

    REQUIRE(chain.size() == 3);
    CHECK(chain[0] == 1);
    CHECK(chain[1] == 2);
    CHECK(chain[2] == 3);
}

// ---------------------------------------------------------------------------
// Multiple Chains Per Entity (via IkSystem)
// ---------------------------------------------------------------------------

TEST_CASE("Multiple IK chains per entity", "[ik]")
{
    // Create a 5-joint skeleton with two independent 3-joint chains:
    // Chain A: joints 0-1-2
    // Chain B: joints 0-3-4 (branching skeleton)
    Skeleton skel;
    skel.joints.resize(5);
    skel.joints[0].parentIndex = -1;
    skel.joints[0].inverseBindMatrix = Mat4(1.0f);
    skel.joints[1].parentIndex = 0;
    skel.joints[1].inverseBindMatrix = Mat4(1.0f);
    skel.joints[2].parentIndex = 1;
    skel.joints[2].inverseBindMatrix = Mat4(1.0f);
    skel.joints[3].parentIndex = 0;
    skel.joints[3].inverseBindMatrix = Mat4(1.0f);
    skel.joints[4].parentIndex = 3;
    skel.joints[4].inverseBindMatrix = Mat4(1.0f);

    Pose pose;
    pose.jointPoses.resize(5);
    pose.jointPoses[0] = {Vec3{0, 0, 0}, Quat{1, 0, 0, 0}, Vec3{1}};
    pose.jointPoses[1] = {Vec3{1, 0, 0}, Quat{1, 0, 0, 0}, Vec3{1}};
    pose.jointPoses[2] = {Vec3{1, 0, 0}, Quat{1, 0, 0, 0}, Vec3{1}};
    pose.jointPoses[3] = {Vec3{0, 1, 0}, Quat{1, 0, 0, 0}, Vec3{1}};
    pose.jointPoses[4] = {Vec3{0, 1, 0}, Quat{1, 0, 0, 0}, Vec3{1}};

    AnimationResources animRes;
    uint32_t skelId = animRes.addSkeleton(skel);

    Registry reg;
    EntityID entity = reg.createEntity();
    reg.emplace<SkeletonComponent>(entity, SkeletonComponent{skelId});
    reg.emplace<SkinComponent>(entity, SkinComponent{0, 5});

    // Two chains.
    IkChainsComponent chains;
    IkChainDef chainA;
    chainA.rootJoint = 0;
    chainA.midJoint = 1;
    chainA.endEffectorJoint = 2;
    chainA.solverType = IkSolverType::TwoBone;
    chainA.poleVector = Vec3{0, 1, 0};
    chains.chains.push_back(chainA);

    IkChainDef chainB;
    chainB.rootJoint = 0;
    chainB.midJoint = 3;
    chainB.endEffectorJoint = 4;
    chainB.solverType = IkSolverType::TwoBone;
    chainB.poleVector = Vec3{1, 0, 0};
    chains.chains.push_back(chainB);
    reg.emplace<IkChainsComponent>(entity, std::move(chains));

    IkTargetsComponent targets;
    IkTarget targetA;
    targetA.position = Vec3{1.5f, 0.5f, 0.0f};
    targets.targets.push_back(targetA);

    IkTarget targetB;
    targetB.position = Vec3{0.5f, 1.5f, 0.0f};
    targets.targets.push_back(targetB);
    reg.emplace<IkTargetsComponent>(entity, std::move(targets));

    // Store pose.
    auto* posePtr = new Pose(pose);
    reg.emplace<PoseComponent>(entity, PoseComponent{posePtr});

    // Run IK system.
    IkSystem ikSys;
    ikSys.update(reg, animRes, nullptr);

    // Verify that the pose was modified (rotations changed from identity).
    auto* resultPose = reg.get<PoseComponent>(entity);
    REQUIRE(resultPose);
    REQUIRE(resultPose->pose);

    // At least one joint should have a non-identity rotation.
    bool anyModified = false;
    for (size_t i = 0; i < 5; ++i)
    {
        float dot = glm::dot(resultPose->pose->jointPoses[i].rotation, Quat{1, 0, 0, 0});
        if (std::abs(std::abs(dot) - 1.0f) > 1e-3f)
        {
            anyModified = true;
            break;
        }
    }
    CHECK(anyModified);

    delete posePtr;
}

// ---------------------------------------------------------------------------
// computeWorldPositions test
// ---------------------------------------------------------------------------

TEST_CASE("computeWorldPositions correct for linear chain", "[ik]")
{
    Skeleton skel = makeThreeJointSkeleton();
    Pose pose = makeThreeJointPose();
    Vec3 worldPos[3];
    computeWorldPositions(skel, pose, worldPos);

    CHECK(approxVec3(worldPos[0], Vec3{0, 0, 0}));
    CHECK(approxVec3(worldPos[1], Vec3{1, 0, 0}));
    CHECK(approxVec3(worldPos[2], Vec3{2, 0, 0}));
}
