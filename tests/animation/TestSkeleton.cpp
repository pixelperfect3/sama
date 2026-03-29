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
    skel.joints.resize(4);

    // root
    skel.joints[0].parentIndex = -1;
    skel.joints[0].nameHash = fnv1a("root");

    // spine
    skel.joints[1].parentIndex = 0;
    skel.joints[1].nameHash = fnv1a("spine");

    // arm
    skel.joints[2].parentIndex = 1;
    skel.joints[2].nameHash = fnv1a("arm");

    // hand
    skel.joints[3].parentIndex = 2;
    skel.joints[3].nameHash = fnv1a("hand");

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
    CHECK(skel.joints[0].parentIndex == -1);
    CHECK(skel.joints[1].parentIndex == 0);
    CHECK(skel.joints[2].parentIndex == 1);
    CHECK(skel.joints[3].parentIndex == 2);
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
