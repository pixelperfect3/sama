#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/math/Math.h"

using namespace engine::math;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

TEST_CASE("Math: kPi is approximately 3.14159", "[math]")
{
    REQUIRE(kPi == Approx(3.14159265f).epsilon(1e-5f));
}

TEST_CASE("Math: kHalfPi is kPi / 2", "[math]")
{
    REQUIRE(kHalfPi == Approx(kPi / 2.0f).epsilon(1e-6f));
}

TEST_CASE("Math: kTwoPi is kPi * 2", "[math]")
{
    REQUIRE(kTwoPi == Approx(kPi * 2.0f).epsilon(1e-6f));
}

// ---------------------------------------------------------------------------
// Angle conversion
// ---------------------------------------------------------------------------

TEST_CASE("Math: toRadians converts 180 degrees to pi", "[math]")
{
    REQUIRE(toRadians(180.0f) == Approx(kPi).epsilon(1e-6f));
}

TEST_CASE("Math: toDegrees converts pi to 180 degrees", "[math]")
{
    REQUIRE(toDegrees(kPi) == Approx(180.0f).epsilon(1e-4f));
}

TEST_CASE("Math: toRadians / toDegrees round-trip", "[math]")
{
    const float deg = 45.0f;
    REQUIRE(toDegrees(toRadians(deg)) == Approx(deg).epsilon(1e-5f));
}

// ---------------------------------------------------------------------------
// lerp
// ---------------------------------------------------------------------------

TEST_CASE("Math: lerp at t=0 returns a", "[math]")
{
    REQUIRE(lerp(0.0f, 10.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("Math: lerp at t=1 returns b", "[math]")
{
    REQUIRE(lerp(0.0f, 10.0f, 1.0f) == Approx(10.0f));
}

TEST_CASE("Math: lerp at t=0.5 returns midpoint", "[math]")
{
    REQUIRE(lerp(0.0f, 10.0f, 0.5f) == Approx(5.0f));
}

TEST_CASE("Math: lerp with negative range", "[math]")
{
    REQUIRE(lerp(-10.0f, 10.0f, 0.5f) == Approx(0.0f));
}

// ---------------------------------------------------------------------------
// clamp
// ---------------------------------------------------------------------------

TEST_CASE("Math: clamp returns value when inside range", "[math]")
{
    REQUIRE(clamp(5.0f, 0.0f, 10.0f) == Approx(5.0f));
}

TEST_CASE("Math: clamp clamps to lo", "[math]")
{
    REQUIRE(clamp(-1.0f, 0.0f, 10.0f) == Approx(0.0f));
}

TEST_CASE("Math: clamp clamps to hi", "[math]")
{
    REQUIRE(clamp(11.0f, 0.0f, 10.0f) == Approx(10.0f));
}

// ---------------------------------------------------------------------------
// saturate
// ---------------------------------------------------------------------------

TEST_CASE("Math: saturate passes through values in [0, 1]", "[math]")
{
    REQUIRE(saturate(0.5f) == Approx(0.5f));
}

TEST_CASE("Math: saturate clamps negative to 0", "[math]")
{
    REQUIRE(saturate(-5.0f) == Approx(0.0f));
}

TEST_CASE("Math: saturate clamps above 1 to 1", "[math]")
{
    REQUIRE(saturate(2.0f) == Approx(1.0f));
}

// ---------------------------------------------------------------------------
// remap
// ---------------------------------------------------------------------------

TEST_CASE("Math: remap maps midpoint correctly", "[math]")
{
    REQUIRE(remap(5.0f, 0.0f, 10.0f, 0.0f, 100.0f) == Approx(50.0f));
}

TEST_CASE("Math: remap maps lo to outLo", "[math]")
{
    REQUIRE(remap(0.0f, 0.0f, 10.0f, 20.0f, 40.0f) == Approx(20.0f));
}

TEST_CASE("Math: remap maps hi to outHi", "[math]")
{
    REQUIRE(remap(10.0f, 0.0f, 10.0f, 20.0f, 40.0f) == Approx(40.0f));
}

// ---------------------------------------------------------------------------
// smoothstep
// ---------------------------------------------------------------------------

TEST_CASE("Math: smoothstep at lo returns 0", "[math]")
{
    REQUIRE(smoothstep(0.0f, 1.0f, 0.0f) == Approx(0.0f));
}

TEST_CASE("Math: smoothstep at hi returns 1", "[math]")
{
    REQUIRE(smoothstep(0.0f, 1.0f, 1.0f) == Approx(1.0f));
}

TEST_CASE("Math: smoothstep at midpoint returns 0.5", "[math]")
{
    REQUIRE(smoothstep(0.0f, 1.0f, 0.5f) == Approx(0.5f));
}

// ---------------------------------------------------------------------------
// approxEqual
// ---------------------------------------------------------------------------

TEST_CASE("Math: approxEqual returns true for identical floats", "[math]")
{
    REQUIRE(approxEqual(1.0f, 1.0f));
}

TEST_CASE("Math: approxEqual returns true within epsilon", "[math]")
{
    REQUIRE(approxEqual(1.0f, 1.0f + kEpsilon * 0.5f));
}

TEST_CASE("Math: approxEqual returns false outside epsilon", "[math]")
{
    REQUIRE_FALSE(approxEqual(1.0f, 1.1f));
}

TEST_CASE("Math: approxEqual works on Vec3", "[math]")
{
    REQUIRE(approxEqual(Vec3(1, 2, 3), Vec3(1, 2, 3)));
    REQUIRE_FALSE(approxEqual(Vec3(1, 2, 3), Vec3(1, 2, 4)));
}

// ---------------------------------------------------------------------------
// isPowerOfTwo
// ---------------------------------------------------------------------------

TEST_CASE("Math: isPowerOfTwo identifies powers of two", "[math]")
{
    REQUIRE(isPowerOfTwo(1));
    REQUIRE(isPowerOfTwo(2));
    REQUIRE(isPowerOfTwo(4));
    REQUIRE(isPowerOfTwo(1024));
}

TEST_CASE("Math: isPowerOfTwo rejects non-powers", "[math]")
{
    REQUIRE_FALSE(isPowerOfTwo(0));
    REQUIRE_FALSE(isPowerOfTwo(3));
    REQUIRE_FALSE(isPowerOfTwo(5));
    REQUIRE_FALSE(isPowerOfTwo(1023));
}

// ---------------------------------------------------------------------------
// nextPowerOfTwo
// ---------------------------------------------------------------------------

TEST_CASE("Math: nextPowerOfTwo of a power of two returns itself", "[math]")
{
    REQUIRE(nextPowerOfTwo(8) == 8);
    REQUIRE(nextPowerOfTwo(1) == 1);
}

TEST_CASE("Math: nextPowerOfTwo rounds up to next power", "[math]")
{
    REQUIRE(nextPowerOfTwo(5) == 8);
    REQUIRE(nextPowerOfTwo(9) == 16);
    REQUIRE(nextPowerOfTwo(1000) == 1024);
}

TEST_CASE("Math: nextPowerOfTwo of 0 returns 1", "[math]")
{
    REQUIRE(nextPowerOfTwo(0) == 1);
}
