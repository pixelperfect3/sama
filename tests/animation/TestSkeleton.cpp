#include <catch2/catch_test_macros.hpp>

#include "engine/animation/Hash.h"
#include "engine/animation/Skeleton.h"

using namespace engine::animation;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Skeleton make4JointChain()
{
    Skeleton skel;
    // Skeleton::resize sizes parentIndices / inverseBindMatrices /
    // nameHashes / restPoses in lockstep.  See Skeleton.h hot/cold split.
    skel.resize(4);

    // root
    skel.parentIndices[0] = -1;
    skel.nameHashes[0] = fnv1a("root");

    // spine
    skel.parentIndices[1] = 0;
    skel.nameHashes[1] = fnv1a("spine");

    // arm
    skel.parentIndices[2] = 1;
    skel.nameHashes[2] = fnv1a("arm");

    // hand
    skel.parentIndices[3] = 2;
    skel.nameHashes[3] = fnv1a("hand");

    return skel;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("Skeleton jointCount for 4-joint chain", "[animation]")
{
    Skeleton skel = make4JointChain();
    CHECK(skel.jointCount() == 4);
}

TEST_CASE("Skeleton parent indices are correct", "[animation]")
{
    Skeleton skel = make4JointChain();
    CHECK(skel.parentIndices[0] == -1);
    CHECK(skel.parentIndices[1] == 0);
    CHECK(skel.parentIndices[2] == 1);
    CHECK(skel.parentIndices[3] == 2);
}

TEST_CASE("Skeleton hot/cold split — parallel arrays stay in lockstep after resize", "[animation]")
{
    // resize() must size all four parallel arrays.  Without this lockstep
    // any caller that writes nameHashes[i] after resize would index out of
    // bounds.  Pins the contract for #M1.
    Skeleton skel;
    skel.resize(7);
    CHECK(skel.parentIndices.size() == 7);
    CHECK(skel.inverseBindMatrices.size() == 7);
    CHECK(skel.nameHashes.size() == 7);
    CHECK(skel.restPoses.size() == 7);
    CHECK(skel.jointCount() == 7);
    // Default values: parents = -1 (root), matrices = identity, hashes = 0.
    for (size_t i = 0; i < 7; ++i)
    {
        CHECK(skel.parentIndices[i] == -1);
        CHECK(skel.nameHashes[i] == 0);
        CHECK(skel.inverseBindMatrices[i] == engine::math::Mat4(1.0F));
    }
}

TEST_CASE("Skeleton::joint(i) synthesises Joint from parallel arrays", "[animation]")
{
    // Backward-compat helper for setup-time consumers.  Reads from the three
    // parallel arrays and returns a Joint by value.
    Skeleton skel = make4JointChain();
    Joint j = skel.joint(2);
    CHECK(j.parentIndex == 1);
    CHECK(j.nameHash == fnv1a("arm"));
}

TEST_CASE("findJoint with known hash returns correct index", "[animation]")
{
    Skeleton skel = make4JointChain();
    CHECK(skel.findJoint(fnv1a("root")) == 0);
    CHECK(skel.findJoint(fnv1a("spine")) == 1);
    CHECK(skel.findJoint(fnv1a("arm")) == 2);
    CHECK(skel.findJoint(fnv1a("hand")) == 3);
}

TEST_CASE("findJoint with unknown hash returns -1", "[animation]")
{
    Skeleton skel = make4JointChain();
    CHECK(skel.findJoint(fnv1a("nonexistent")) == -1);
    CHECK(skel.findJoint(0) == -1);
}
