#include <catch2/catch_test_macros.hpp>

#include "engine/input/InputState.h"

using namespace engine::input;

TEST_CASE("GyroState defaults to unavailable", "[input][gyro]")
{
    InputState state;
    REQUIRE_FALSE(state.gyro().available);
    REQUIRE(state.gyro().pitchRate == 0.0f);
    REQUIRE(state.gyro().yawRate == 0.0f);
    REQUIRE(state.gyro().rollRate == 0.0f);
    REQUIRE(state.gyro().gravityX == 0.0f);
    REQUIRE(state.gyro().gravityY == 0.0f);
    REQUIRE(state.gyro().gravityZ == 0.0f);
}

TEST_CASE("GyroState can be written and read", "[input][gyro]")
{
    InputState state;
    state.gyro_.available = true;
    state.gyro_.pitchRate = 0.5f;
    state.gyro_.yawRate = -0.3f;
    state.gyro_.rollRate = 0.1f;
    state.gyro_.gravityX = 0.0f;
    state.gyro_.gravityY = -9.81f;
    state.gyro_.gravityZ = 0.0f;

    REQUIRE(state.gyro().available);
    REQUIRE(state.gyro().pitchRate == 0.5f);
    REQUIRE(state.gyro().yawRate == -0.3f);
    REQUIRE(state.gyro().rollRate == 0.1f);
    REQUIRE(state.gyro().gravityY == -9.81f);
}

TEST_CASE("GyroState rates represent angular velocity", "[input][gyro]")
{
    // Verify that gyro rates can integrate into orientation over time.
    InputState state;
    state.gyro_.available = true;
    state.gyro_.pitchRate = 1.0f;  // 1 rad/sec

    float dt = 0.016f;  // ~60 fps
    float pitchAngle = state.gyro().pitchRate * dt;
    REQUIRE(pitchAngle > 0.0f);
    REQUIRE(pitchAngle < 0.02f);  // ~0.016 radians per frame
}
