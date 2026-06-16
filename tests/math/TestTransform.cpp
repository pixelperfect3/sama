#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
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

// ---------------------------------------------------------------------------
// inverseTRS — fast TRS inverse, must agree with glm::inverse to ~ulp.
//
// Audit item line 125.  Test fixtures cover the common TRS patterns:
// identity, pure translation, pure rotation, scaled, full TRS, and a
// non-uniform-scale case (where the structure-specific math is most
// likely to diverge from the general inverse if I got the algebra wrong).
// ---------------------------------------------------------------------------

namespace
{

// Compare two Mat4s for element-wise approximate equality.
bool approxEqualMat(const Mat4& a, const Mat4& b, float eps = 1e-4f)
{
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (std::abs(a[col][row] - b[col][row]) > eps)
            {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("Transform: inverseTRS of identity is identity", "[transform][inverse-trs]")
{
    const Mat4 inv = inverseTRS(Mat4(1.0f));
    REQUIRE(approxEqualMat(inv, Mat4(1.0f)));
}

TEST_CASE("Transform: inverseTRS of pure translation", "[transform][inverse-trs]")
{
    const Mat4 m = makeTranslation(Vec3(3, -4, 5));
    const Mat4 inv = inverseTRS(m);
    const Mat4 expected = glm::inverse(m);
    REQUIRE(approxEqualMat(inv, expected));
    // Round-trip — composing should give identity.
    REQUIRE(approxEqualMat(m * inv, Mat4(1.0f)));
}

TEST_CASE("Transform: inverseTRS of pure rotation", "[transform][inverse-trs]")
{
    const Quat rot = glm::angleAxis(glm::radians(37.0f), glm::normalize(Vec3(1, 2, 3)));
    const Mat4 m = makeRotation(rot);
    const Mat4 inv = inverseTRS(m);
    REQUIRE(approxEqualMat(inv, glm::inverse(m)));
    REQUIRE(approxEqualMat(m * inv, Mat4(1.0f)));
}

TEST_CASE("Transform: inverseTRS of uniform scale", "[transform][inverse-trs]")
{
    const Mat4 m = makeScale(Vec3(2.5f));
    const Mat4 inv = inverseTRS(m);
    REQUIRE(approxEqualMat(inv, glm::inverse(m)));
    REQUIRE(approxEqualMat(m * inv, Mat4(1.0f)));
}

TEST_CASE("Transform: inverseTRS of full T*R*S", "[transform][inverse-trs]")
{
    const Vec3 t(1.5f, -2.0f, 3.0f);
    const Quat r = glm::angleAxis(glm::radians(60.0f), glm::normalize(Vec3(0, 1, 0)));
    const Vec3 s(1.2f, 0.8f, 1.5f);
    const Mat4 m = makeTRS(t, r, s);
    const Mat4 inv = inverseTRS(m);

    // Within a slightly looser epsilon — the non-uniform-scale + rotation
    // chain accumulates fp noise relative to glm::inverse's cofactor form.
    REQUIRE(approxEqualMat(inv, glm::inverse(m), 1e-3f));
    REQUIRE(approxEqualMat(m * inv, Mat4(1.0f), 1e-3f));
}

TEST_CASE("Transform: inverseTRS of full TRS roundtrips a point", "[transform][inverse-trs]")
{
    // Most physics-relevant assertion: inverseTRS(parent) * worldPos
    // gives the same parent-local position as glm::inverse(parent) *
    // worldPos.  That's exactly the call site at PhysicsSystem.cpp:172.
    const Vec3 t(10.0f, 5.0f, -3.0f);
    const Quat r = glm::angleAxis(glm::radians(45.0f), glm::normalize(Vec3(1, 1, 0)));
    const Vec3 s(1.5f, 0.5f, 2.0f);
    const Mat4 parentWorld = makeTRS(t, r, s);

    const Vec4 worldPos(7.0f, -1.0f, 4.0f, 1.0f);
    const Vec4 localFast = inverseTRS(parentWorld) * worldPos;
    const Vec4 localRef = glm::inverse(parentWorld) * worldPos;

    REQUIRE(approxEqual(Vec3(localFast), Vec3(localRef), 1e-4f));
}

// ---------------------------------------------------------------------------
// Microbenchmark — audit item line 125.  [!benchmark]-tagged so it
// doesn't run by default; trigger with `build/engine_tests
// "[inverse-trs-bench]"`.  Reports go to stdout; the CHECK at the end is
// a loose ceiling that catches a 10x regression rather than a tight
// gate.
// ---------------------------------------------------------------------------

// `asm volatile` barriers — standard benchmark idiom to defeat the
// compiler's loop-invariant code motion.  `doNotOptimize(x)` tells the
// optimiser "this value might change" so it can't constant-fold past it;
// `clobber()` flushes pretend writes so it can't hoist the loop body out.
template <typename T>
inline void doNotOptimize(T& value)
{
    asm volatile("" : "+r,m"(value) : : "memory");
}

inline void clobber()
{
    asm volatile("" : : : "memory");
}

TEST_CASE("BENCH: inverseTRS vs glm::inverse — TRS matrix",
          "[inverse-trs-bench][!benchmark]")
{
    // Representative TRS matrix — non-uniform scale, off-axis rotation,
    // non-zero translation, so the general inverse can't shortcut.
    Vec3 trans(2.5f, -1.0f, 3.0f);
    Quat rot = glm::angleAxis(glm::radians(37.0f), glm::normalize(Vec3(1.0f, 2.0f, 3.0f)));
    Vec3 scale(1.4f, 0.7f, 2.1f);
    Mat4 parentWorld = makeTRS(trans, rot, scale);

    constexpr int kIterations = 1'000'000;

    // Warm-up.
    Mat4 sink(0.0f);
    for (int i = 0; i < 1000; ++i)
    {
        doNotOptimize(parentWorld);
        sink = inverseTRS(parentWorld);
        doNotOptimize(sink);
        sink = glm::inverse(parentWorld);
        doNotOptimize(sink);
    }

    using Clock = std::chrono::steady_clock;

    const auto fastStart = Clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        doNotOptimize(parentWorld);
        sink = inverseTRS(parentWorld);
        doNotOptimize(sink);
    }
    clobber();
    const auto fastNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            Clock::now() - fastStart)
                            .count();

    const auto refStart = Clock::now();
    for (int i = 0; i < kIterations; ++i)
    {
        doNotOptimize(parentWorld);
        sink = glm::inverse(parentWorld);
        doNotOptimize(sink);
    }
    clobber();
    const auto refNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - refStart)
                           .count();

    const double fastNsPerCall = static_cast<double>(fastNs) / static_cast<double>(kIterations);
    const double refNsPerCall = static_cast<double>(refNs) / static_cast<double>(kIterations);
    const double speedup = refNsPerCall / fastNsPerCall;

    std::printf("\n[BENCH inverseTRS] %d iters: %.1f ns/call (fast) vs %.1f ns/call "
                "(glm::inverse) — %.2fx speedup\n",
                kIterations, fastNsPerCall, refNsPerCall, speedup);

    // Dummy use so the sink isn't optimised away.
    CHECK(std::isfinite(sink[0][0]));
    CHECK(fastNsPerCall < refNsPerCall);  // Sanity: fast IS faster.
}
