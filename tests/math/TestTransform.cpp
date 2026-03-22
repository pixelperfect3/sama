#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/math/Math.h"
#include "engine/math/Transform.h"

using namespace engine::math;
using Catch::Approx;

// ---------------------------------------------------------------------------
// makeTranslation
// ---------------------------------------------------------------------------

TEST_CASE("Transform: makeTranslation moves a point", "[transform]")
{
    const Mat4 m = makeTranslation(Vec3(3, 4, 5));
    const Vec4 result = m * Vec4(0, 0, 0, 1);
    REQUIRE(result.x == Approx(3.0f));
    REQUIRE(result.y == Approx(4.0f));
    REQUIRE(result.z == Approx(5.0f));
}

TEST_CASE("Transform: makeTranslation of zero is identity", "[transform]")
{
    const Mat4 m = makeTranslation(Vec3(0, 0, 0));
    REQUIRE(approxEqual(m, Mat4(1.0f)));
}

// ---------------------------------------------------------------------------
// makeScale
// ---------------------------------------------------------------------------

TEST_CASE("Transform: makeScale scales a point", "[transform]")
{
    const Mat4 m = makeScale(Vec3(2, 3, 4));
    const Vec4 result = m * Vec4(1, 1, 1, 1);
    REQUIRE(result.x == Approx(2.0f));
    REQUIRE(result.y == Approx(3.0f));
    REQUIRE(result.z == Approx(4.0f));
}

TEST_CASE("Transform: makeScale of (1,1,1) is identity", "[transform]")
{
    const Mat4 m = makeScale(Vec3(1, 1, 1));
    REQUIRE(approxEqual(m, Mat4(1.0f)));
}

// ---------------------------------------------------------------------------
// makeRotation
// ---------------------------------------------------------------------------

TEST_CASE("Transform: makeRotation 90 degrees around Y rotates X to -Z", "[transform]")
{
    const Quat r = glm::angleAxis(kHalfPi, Vec3(0, 1, 0));
    const Mat4 m = makeRotation(r);
    const Vec4 p = m * Vec4(1, 0, 0, 1);
    REQUIRE(p.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(p.y == Approx(0.0f).margin(1e-5f));
    REQUIRE(p.z == Approx(-1.0f).margin(1e-5f));
}

TEST_CASE("Transform: makeRotation identity quaternion is identity matrix", "[transform]")
{
    const Mat4 m = makeRotation(Quat(1, 0, 0, 0));
    REQUIRE(approxEqual(m, Mat4(1.0f)));
}

// ---------------------------------------------------------------------------
// makeTRS
// ---------------------------------------------------------------------------

TEST_CASE("Transform: makeTRS with identity R and S=1 equals translation", "[transform]")
{
    const Vec3 t(5, 6, 7);
    const Mat4 trs = makeTRS(t, Quat(1, 0, 0, 0), Vec3(1, 1, 1));
    const Mat4 expected = makeTranslation(t);
    REQUIRE(approxEqual(trs, expected, 1e-5f));
}

TEST_CASE("Transform: makeTRS applies scale before rotation before translation", "[transform]")
{
    // Scale x2, rotate 90deg around Y, translate (10, 0, 0).
    // A point at (1,0,0) after scale = (2,0,0), after rotate = (0,0,-2), after translate =
    // (10,0,-2).
    const Vec3 t(10, 0, 0);
    const Quat r = glm::angleAxis(kHalfPi, Vec3(0, 1, 0));
    const Vec3 s(2, 2, 2);

    const Mat4 m = makeTRS(t, r, s);
    const Vec4 result = m * Vec4(1, 0, 0, 1);

    REQUIRE(result.x == Approx(10.0f).margin(1e-4f));
    REQUIRE(result.y == Approx(0.0f).margin(1e-4f));
    REQUIRE(result.z == Approx(-2.0f).margin(1e-4f));
}

// ---------------------------------------------------------------------------
// decomposeTRS round-trip
// ---------------------------------------------------------------------------

TEST_CASE("Transform: decomposeTRS extracts translation correctly", "[transform]")
{
    const Vec3 t(3, 7, -2);
    const Mat4 m = makeTRS(t, Quat(1, 0, 0, 0), Vec3(1, 1, 1));

    Vec3 outT;
    Quat outR;
    Vec3 outS;
    REQUIRE(decomposeTRS(m, outT, outR, outS));
    REQUIRE(approxEqual(outT, t, 1e-4f));
}

TEST_CASE("Transform: decomposeTRS extracts scale correctly", "[transform]")
{
    const Vec3 s(2, 3, 4);
    const Mat4 m = makeTRS(Vec3(0), Quat(1, 0, 0, 0), s);

    Vec3 outT;
    Quat outR;
    Vec3 outS;
    REQUIRE(decomposeTRS(m, outT, outR, outS));
    REQUIRE(approxEqual(outS, s, 1e-4f));
}

TEST_CASE("Transform: decomposeTRS round-trips a full TRS", "[transform]")
{
    const Vec3 t(1, 2, 3);
    const Quat r = glm::angleAxis(toRadians(45.0f), glm::normalize(Vec3(1, 1, 0)));
    const Vec3 s(1.5f, 2.0f, 0.5f);

    const Mat4 m = makeTRS(t, r, s);

    Vec3 outT;
    Quat outR;
    Vec3 outS;
    REQUIRE(decomposeTRS(m, outT, outR, outS));

    REQUIRE(approxEqual(outT, t, 1e-4f));
    REQUIRE(approxEqual(outS, s, 1e-4f));

    // Reconstructed matrix from extracted TRS should match original.
    const Mat4 rebuilt = makeTRS(outT, outR, outS);
    REQUIRE(approxEqual(rebuilt, m, 1e-4f));
}

// ---------------------------------------------------------------------------
// Projection sanity checks
// ---------------------------------------------------------------------------

TEST_CASE("Transform: makePerspective produces a non-identity matrix", "[transform]")
{
    const Mat4 p = makePerspective(toRadians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    REQUIRE_FALSE(approxEqual(p, Mat4(1.0f)));
}

TEST_CASE("Transform: makeOrtho produces a non-identity matrix", "[transform]")
{
    const Mat4 o = makeOrtho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    REQUIRE_FALSE(approxEqual(o, Mat4(1.0f)));
}

TEST_CASE("Transform: makeLookAt produces a view matrix (origin is not (0,0,1) after transform)",
          "[transform]")
{
    const Mat4 view = makeLookAt(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    // The camera is at (0,0,5) looking toward origin.
    // Transforming the eye position by the view matrix should put it at the origin in view space.
    const Vec4 eyeInView = view * Vec4(0, 0, 5, 1);
    REQUIRE(approxEqual(Vec3(eyeInView), Vec3(0, 0, 0), 1e-4f));
}
