#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    skel.resize(1);
    skel.parentIndices[0] = -1;
    skel.nameHashes[0] = 1;
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

// ---------------------------------------------------------------------------
// Microbenchmark — A/B harness for the audit's AnimationSampler items
// (lines 122-123 in docs/PERF_AUDIT_2026-05-25.md).
//
// Build a representative 64-joint skeleton + 64-channel clip with 4
// keyframes per channel.  Time N iterations of sampleClip, print
// ns/call.  Tagged [!benchmark] so it runs only when explicitly
// requested:
//
//   build/engine_tests "[anim-bench]"
//
// Reports go to stdout, not Catch2 assertions — the bench is a
// measurement tool, not a regression gate (it's noise-sensitive).
// ---------------------------------------------------------------------------

namespace
{

Skeleton makeBenchSkeleton(uint32_t jointCount)
{
    Skeleton skel;
    skel.resize(jointCount);
    for (uint32_t i = 0; i < jointCount; ++i)
    {
        // Build a left-leaning chain — parent is previous joint.
        skel.parentIndices[i] = (i == 0) ? -1 : static_cast<int32_t>(i - 1);
        skel.nameHashes[i] = i + 1;
        // Rest pose: small per-joint offset so sampling has work to do
        // against a non-identity baseline.
        skel.restPoses[i].position = Vec3{0.1F * static_cast<float>(i), 0.0F, 0.0F};
        skel.restPoses[i].rotation =
            glm::angleAxis(glm::radians(5.0F * static_cast<float>(i)), Vec3{0.0F, 1.0F, 0.0F});
        skel.restPoses[i].scale = Vec3{1.0F};
    }
    return skel;
}

AnimationClip makeBenchClip(uint32_t channelCount)
{
    AnimationClip clip;
    clip.duration = 1.0F;
    clip.channels.reserve(channelCount);
    for (uint32_t i = 0; i < channelCount; ++i)
    {
        JointChannel chan;
        chan.jointIndex = i;
        chan.positions.reserve(4);
        chan.rotations.reserve(4);
        chan.scales.reserve(4);
        for (uint32_t k = 0; k < 4; ++k)
        {
            const float t = static_cast<float>(k) / 3.0F;
            chan.positions.push_back({t, Vec3{static_cast<float>(i) * 0.5F + t, 0.0F, 0.0F}});
            chan.rotations.push_back(
                {t, glm::angleAxis(glm::radians(45.0F * t), Vec3{0.0F, 1.0F, 0.0F})});
            chan.scales.push_back({t, Vec3{1.0F + t * 0.1F}});
        }
        clip.channels.push_back(std::move(chan));
    }
    return clip;
}

}  // namespace

TEST_CASE("BENCH: sampleClip 64 joints / 64 channels / 4 keyframes", "[anim-bench][!benchmark]")
{
    constexpr uint32_t kJointCount = 64;
    constexpr uint32_t kChannelCount = 64;
    constexpr int kIterations = 50000;

    Skeleton skel = makeBenchSkeleton(kJointCount);
    AnimationClip clip = makeBenchClip(kChannelCount);
    Pose pose;

    // Warm up — first call resizes outPose.jointPoses; subsequent calls
    // reuse storage.  Without warmup the very first call eats an
    // allocation that skews the per-call number.
    for (int i = 0; i < 100; ++i)
    {
        sampleClip(clip, skel, 0.5F, pose);
    }

    // Vary `time` so the keyframe-pair binary search hits different
    // points (avoids a degenerate single-pair always-true cache).
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        const float time = 0.001F * static_cast<float>(i % 1000);
        sampleClip(clip, skel, time, pose);
    }
    const auto elapsed = Clock::now() - start;

    const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const double nsPerCall = static_cast<double>(totalNs) / static_cast<double>(kIterations);

    std::printf("\n[BENCH sampleClip] %d iters, %u joints / %u channels: %.0f ns/call\n",
                kIterations, kJointCount, kChannelCount, nsPerCall);

    // Loose sanity bound — fails if a future change regresses 10×.
    CHECK(nsPerCall < 50000.0);  // 50 µs/call is way over budget; real is ~1-5 µs.
}

TEST_CASE("BENCH: sampleClip 64 joints / 32 channels (half untouched)",
          "[anim-bench][!benchmark]")
{
    // Exercises the "untouched joint" path: half the joints have no
    // channels at all and need rest-pose init.
    constexpr uint32_t kJointCount = 64;
    constexpr uint32_t kChannelCount = 32;
    constexpr int kIterations = 50000;

    Skeleton skel = makeBenchSkeleton(kJointCount);
    AnimationClip clip = makeBenchClip(kChannelCount);
    Pose pose;

    for (int i = 0; i < 100; ++i)
    {
        sampleClip(clip, skel, 0.5F, pose);
    }

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        const float time = 0.001F * static_cast<float>(i % 1000);
        sampleClip(clip, skel, time, pose);
    }
    const auto elapsed = Clock::now() - start;

    const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const double nsPerCall = static_cast<double>(totalNs) / static_cast<double>(kIterations);

    std::printf("[BENCH sampleClip half-untouched] %d iters, %u joints / %u channels: %.0f ns/call\n",
                kIterations, kJointCount, kChannelCount, nsPerCall);

    CHECK(nsPerCall < 50000.0);
}

namespace
{
// Same shape as makeBenchClip but every channel has IDENTICAL keyframe
// values across all 4 keyframes — exercises the equal-kf fast path in
// `sampleChannel` (audit line 123).  Pattern shows up in real glTFs
// exported from tools that stamp the bind pose at every keyframe for
// joints the artist never moved.
AnimationClip makeStaticBenchClip(uint32_t channelCount)
{
    AnimationClip clip;
    clip.duration = 1.0F;
    clip.channels.reserve(channelCount);
    for (uint32_t i = 0; i < channelCount; ++i)
    {
        JointChannel chan;
        chan.jointIndex = i;
        const Vec3 staticPos{static_cast<float>(i) * 0.5F, 0.0F, 0.0F};
        const Quat staticRot =
            glm::angleAxis(glm::radians(5.0F * static_cast<float>(i)), Vec3{0.0F, 1.0F, 0.0F});
        const Vec3 staticScale{1.0F};
        chan.positions.reserve(4);
        chan.rotations.reserve(4);
        chan.scales.reserve(4);
        for (uint32_t k = 0; k < 4; ++k)
        {
            const float t = static_cast<float>(k) / 3.0F;
            chan.positions.push_back({t, staticPos});
            chan.rotations.push_back({t, staticRot});
            chan.scales.push_back({t, staticScale});
        }
        clip.channels.push_back(std::move(chan));
    }
    return clip;
}
}  // namespace

TEST_CASE("BENCH: sampleClip 64 joints / 64 channels with STATIC keyframes (fast path)",
          "[anim-bench][!benchmark]")
{
    // Every channel has kf1.value == kf2.value, so the equal-kf fast
    // path (audit line 123) fires for every sampleChannel call.  This
    // is the workload that motivates that audit item — slerp is ~30
    // muls + a cos, all skipped here.
    constexpr uint32_t kJointCount = 64;
    constexpr uint32_t kChannelCount = 64;
    constexpr int kIterations = 50000;

    Skeleton skel = makeBenchSkeleton(kJointCount);
    AnimationClip clip = makeStaticBenchClip(kChannelCount);
    Pose pose;

    for (int i = 0; i < 100; ++i)
    {
        sampleClip(clip, skel, 0.5F, pose);
    }

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        const float time = 0.001F * static_cast<float>(i % 1000);
        sampleClip(clip, skel, time, pose);
    }
    const auto elapsed = Clock::now() - start;

    const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    const double nsPerCall = static_cast<double>(totalNs) / static_cast<double>(kIterations);

    std::printf("[BENCH sampleClip static-kfs] %d iters, %u joints / %u channels: %.0f ns/call\n",
                kIterations, kJointCount, kChannelCount, nsPerCall);

    CHECK(nsPerCall < 50000.0);
}
