#include <catch2/catch_test_macros.hpp>

#include "engine/math/Frustum.h"
#include "engine/math/Math.h"
#include "engine/math/Transform.h"

using namespace engine::math;

// Build a simple perspective VP matrix for testing.
// Camera at origin looking down -Z. Near=0.1, Far=100.
static Mat4 makeTestVP()
{
    const Mat4 proj = makePerspective(toRadians(90.0f), 1.0f, 0.1f, 100.0f);
    const Mat4 view = makeLookAt(Vec3(0, 0, 0), Vec3(0, 0, -1), Vec3(0, 1, 0));
    return proj * view;
}

// ---------------------------------------------------------------------------
// Sphere containment
// ---------------------------------------------------------------------------

TEST_CASE("Frustum: sphere directly in front of camera is inside", "[frustum]")
{
    const Frustum f(makeTestVP());
    // Sphere at (0, 0, -10) with radius 1 — well inside the frustum.
    REQUIRE(f.containsSphere(Vec3(0, 0, -10), 1.0f));
}

TEST_CASE("Frustum: sphere far behind the camera is outside", "[frustum]")
{
    const Frustum f(makeTestVP());
    // Sphere behind camera (+Z side).
    REQUIRE_FALSE(f.containsSphere(Vec3(0, 0, 50), 1.0f));
}

TEST_CASE("Frustum: sphere beyond far plane is outside", "[frustum]")
{
    const Frustum f(makeTestVP());
    REQUIRE_FALSE(f.containsSphere(Vec3(0, 0, -200), 1.0f));
}

TEST_CASE("Frustum: sphere in front of near plane is outside", "[frustum]")
{
    const Frustum f(makeTestVP());
    // Sphere between camera and near plane (z > -0.1).
    REQUIRE_FALSE(f.containsSphere(Vec3(0, 0, -0.01f), 0.001f));
}

TEST_CASE("Frustum: sphere far to the left is outside", "[frustum]")
{
    const Frustum f(makeTestVP());
    // 90-degree HFOV, so at z=-10 the left edge is at x=-10.
    REQUIRE_FALSE(f.containsSphere(Vec3(-50, 0, -10), 1.0f));
}

TEST_CASE("Frustum: large sphere straddling a plane is inside (partial overlap)", "[frustum]")
{
    const Frustum f(makeTestVP());
    // Center is outside to the left, but radius is large enough to overlap.
    REQUIRE(f.containsSphere(Vec3(-8, 0, -10), 20.0f));
}

// ---------------------------------------------------------------------------
// AABB containment
// ---------------------------------------------------------------------------

TEST_CASE("Frustum: AABB fully inside frustum is detected", "[frustum]")
{
    const Frustum f(makeTestVP());
    REQUIRE(f.containsAABB(Vec3(-1, -1, -11), Vec3(1, 1, -9)));
}

TEST_CASE("Frustum: AABB fully beyond far plane is outside", "[frustum]")
{
    const Frustum f(makeTestVP());
    REQUIRE_FALSE(f.containsAABB(Vec3(-1, -1, -200), Vec3(1, 1, -150)));
}

TEST_CASE("Frustum: AABB fully behind camera is outside", "[frustum]")
{
    const Frustum f(makeTestVP());
    REQUIRE_FALSE(f.containsAABB(Vec3(-1, -1, 5), Vec3(1, 1, 50)));
}

TEST_CASE("Frustum: AABB straddling the far plane is inside (partial overlap)", "[frustum]")
{
    const Frustum f(makeTestVP());
    // AABB spans from z=-90 to z=-110 — half inside, half outside.
    REQUIRE(f.containsAABB(Vec3(-1, -1, -110), Vec3(1, 1, -90)));
}

TEST_CASE("Frustum: AABB far to the side is outside", "[frustum]")
{
    const Frustum f(makeTestVP());
    REQUIRE_FALSE(f.containsAABB(Vec3(200, -1, -10), Vec3(210, 1, -9)));
}

TEST_CASE("Frustum: zero-size AABB (point) inside frustum is detected", "[frustum]")
{
    const Frustum f(makeTestVP());
    REQUIRE(f.containsAABB(Vec3(0, 0, -10), Vec3(0, 0, -10)));
}

TEST_CASE("Frustum: zero-size AABB (point) outside frustum is rejected", "[frustum]")
{
    const Frustum f(makeTestVP());
    REQUIRE_FALSE(f.containsAABB(Vec3(0, 0, 200), Vec3(0, 0, 200)));
}
