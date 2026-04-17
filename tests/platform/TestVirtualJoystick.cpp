#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "engine/platform/android/VirtualJoystick.h"

using Catch::Matchers::WithinAbs;
using engine::platform::VirtualJoystick;
using engine::platform::VirtualJoystickConfig;

namespace
{
constexpr float kScreenW = 1000.0f;
constexpr float kScreenH = 1000.0f;
constexpr float kEps = 0.01f;

// Default config: center at (0.15, 0.75), radius 0.1 of screen width.
// In pixels: center (150, 750), radius 100px.
constexpr float kCenterX = 150.0f;
constexpr float kCenterY = 750.0f;
constexpr float kRadius = 100.0f;
}  // namespace

TEST_CASE("VirtualJoystick: not touched returns zero", "[platform][input]")
{
    VirtualJoystick joy;
    joy.update(0.0f, 0.0f, false, kScreenW, kScreenH);

    CHECK_FALSE(joy.isTouched());
    CHECK_THAT(joy.direction().x, WithinAbs(0.0f, kEps));
    CHECK_THAT(joy.direction().y, WithinAbs(0.0f, kEps));
}

TEST_CASE("VirtualJoystick: dead zone returns zero direction", "[platform][input]")
{
    VirtualJoystick joy;
    // Touch exactly at center
    joy.update(kCenterX, kCenterY, true, kScreenW, kScreenH);

    CHECK(joy.isTouched());
    CHECK_THAT(joy.direction().x, WithinAbs(0.0f, kEps));
    CHECK_THAT(joy.direction().y, WithinAbs(0.0f, kEps));
}

TEST_CASE("VirtualJoystick: direction at cardinal positions", "[platform][input]")
{
    VirtualJoystick joy;
    VirtualJoystickConfig cfg;
    cfg.deadZone = 0.0f;  // disable dead zone for clean cardinal tests
    joy.setConfig(cfg);

    SECTION("right")
    {
        joy.update(kCenterX + kRadius, kCenterY, true, kScreenW, kScreenH);
        CHECK(joy.isTouched());
        CHECK_THAT(joy.direction().x, WithinAbs(1.0f, kEps));
        CHECK_THAT(joy.direction().y, WithinAbs(0.0f, kEps));
    }

    SECTION("left")
    {
        joy.update(kCenterX - kRadius, kCenterY, true, kScreenW, kScreenH);
        CHECK(joy.isTouched());
        CHECK_THAT(joy.direction().x, WithinAbs(-1.0f, kEps));
        CHECK_THAT(joy.direction().y, WithinAbs(0.0f, kEps));
    }

    SECTION("up — touch above center means lower Y pixel value")
    {
        joy.update(kCenterX, kCenterY - kRadius, true, kScreenW, kScreenH);
        CHECK(joy.isTouched());
        CHECK_THAT(joy.direction().x, WithinAbs(0.0f, kEps));
        CHECK_THAT(joy.direction().y, WithinAbs(1.0f, kEps));
    }

    SECTION("down — touch below center means higher Y pixel value")
    {
        joy.update(kCenterX, kCenterY + kRadius, true, kScreenW, kScreenH);
        CHECK(joy.isTouched());
        CHECK_THAT(joy.direction().x, WithinAbs(0.0f, kEps));
        CHECK_THAT(joy.direction().y, WithinAbs(-1.0f, kEps));
    }
}

TEST_CASE("VirtualJoystick: clamping beyond radius", "[platform][input]")
{
    VirtualJoystick joy;
    VirtualJoystickConfig cfg;
    cfg.deadZone = 0.0f;
    joy.setConfig(cfg);

    // Touch far to the right (within 1.5x activation zone)
    joy.update(kCenterX + kRadius * 1.3f, kCenterY, true, kScreenW, kScreenH);
    CHECK(joy.isTouched());

    float len =
        std::sqrt(joy.direction().x * joy.direction().x + joy.direction().y * joy.direction().y);
    // Should be clamped to at most 1.0
    CHECK_THAT(len, WithinAbs(1.0f, kEps));
}

TEST_CASE("VirtualJoystick: diagonal direction normalization", "[platform][input]")
{
    VirtualJoystick joy;
    VirtualJoystickConfig cfg;
    cfg.deadZone = 0.0f;
    joy.setConfig(cfg);

    // Touch at 45 degrees (right and up)
    float offset = kRadius * 0.5f;
    joy.update(kCenterX + offset, kCenterY - offset, true, kScreenW, kScreenH);
    CHECK(joy.isTouched());

    float len =
        std::sqrt(joy.direction().x * joy.direction().x + joy.direction().y * joy.direction().y);

    // Direction should be a unit-length-ish vector (less than or equal to 1)
    CHECK(len <= 1.0f + kEps);
    // Both components should be roughly equal for a 45-degree touch
    CHECK_THAT(std::abs(joy.direction().x), WithinAbs(std::abs(joy.direction().y), kEps));
    // Both should be positive (right and up)
    CHECK(joy.direction().x > 0.0f);
    CHECK(joy.direction().y > 0.0f);
}

TEST_CASE("VirtualJoystick: deadZone 1.0 returns zero", "[platform][input]")
{
    VirtualJoystick joy;
    VirtualJoystickConfig cfg;
    cfg.deadZone = 1.0f;
    joy.setConfig(cfg);
    joy.update(100.0f, 100.0f, true, 800.0f, 600.0f);
    REQUIRE(joy.direction().x == 0.0f);
    REQUIRE(joy.direction().y == 0.0f);
}

TEST_CASE("VirtualJoystick: touch far outside radius is not touched", "[platform][input]")
{
    VirtualJoystick joy;
    // Touch very far from joystick center
    joy.update(900.0f, 100.0f, true, kScreenW, kScreenH);

    CHECK_FALSE(joy.isTouched());
    CHECK_THAT(joy.direction().x, WithinAbs(0.0f, kEps));
    CHECK_THAT(joy.direction().y, WithinAbs(0.0f, kEps));
}
