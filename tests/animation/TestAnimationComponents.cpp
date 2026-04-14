#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

#include "engine/animation/AnimationComponents.h"
#include "engine/animation/Skeleton.h"

using namespace engine::animation;

// ---------------------------------------------------------------------------
// Size assertions
// ---------------------------------------------------------------------------

TEST_CASE("sizeof(Joint) == 80", "[animation]")
{
    STATIC_CHECK(sizeof(Joint) == 80);
}

TEST_CASE("sizeof(AnimatorComponent) == 36", "[animation]")
{
    STATIC_CHECK(sizeof(AnimatorComponent) == 36);
}

TEST_CASE("sizeof(SkinComponent) == 8", "[animation]")
{
    STATIC_CHECK(sizeof(SkinComponent) == 8);
}

TEST_CASE("sizeof(SkeletonComponent) == 4", "[animation]")
{
    STATIC_CHECK(sizeof(SkeletonComponent) == 4);
}

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

TEST_CASE("Joint default values", "[animation]")
{
    Joint j{};
    CHECK(j.parentIndex == -1);
    CHECK(j.nameHash == 0);
}

TEST_CASE("AnimatorComponent flag constants", "[animation]")
{
    STATIC_CHECK(AnimatorComponent::kFlagLooping == 0x01);
    STATIC_CHECK(AnimatorComponent::kFlagPlaying == 0x02);
    STATIC_CHECK(AnimatorComponent::kFlagBlending == 0x04);
}

TEST_CASE("SkinComponent default values", "[animation]")
{
    SkinComponent sc{};
    CHECK(sc.boneMatrixOffset == 0);
    CHECK(sc.boneCount == 0);
}

TEST_CASE("SkeletonComponent default values", "[animation]")
{
    SkeletonComponent sc{};
    CHECK(sc.skeletonId == 0);
}
