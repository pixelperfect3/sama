#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/input/InputState.h"
#include "engine/input/Key.h"

namespace engine::core
{

// ---------------------------------------------------------------------------
// OrbitCamera -- shared orbit camera used by all demos.
//
// Orbits around a target point with configurable distance, yaw, and pitch.
// Supports right-drag orbit, scroll zoom, and WASD target movement.
// ---------------------------------------------------------------------------

struct OrbitCamera
{
    float distance = 10.0f;
    float yaw = 0.0f;
    float pitch = 20.0f;
    glm::vec3 target = {0, 0, 0};

    [[nodiscard]] glm::vec3 position() const
    {
        float r = glm::radians(pitch);
        float y = glm::radians(yaw);
        return target + glm::vec3(distance * std::cos(r) * std::sin(y), distance * std::sin(r),
                                  distance * std::cos(r) * std::cos(y));
    }

    [[nodiscard]] glm::mat4 view() const
    {
        return glm::lookAt(position(), target, glm::vec3(0, 1, 0));
    }

    // Process orbit drag deltas (in raw mouse-pixel units).
    // sensitivity converts pixels to degrees (default 0.3 matches existing demos).
    void orbit(float deltaYaw, float deltaPitch, float sensitivity = 0.3f)
    {
        yaw += deltaYaw * sensitivity;
        pitch += deltaPitch * sensitivity;
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    }

    // Scroll zoom.  Positive scrollDelta zooms in (reduces distance).
    void zoom(float scrollDelta, float speed = 1.0f, float minDist = 1.0f, float maxDist = 50.0f)
    {
        distance -= scrollDelta * speed;
        distance = glm::clamp(distance, minDist, maxDist);
    }

    // WASD/QE target movement relative to camera yaw.
    void moveTarget(const input::InputState& input, float dt, float speed = 3.0f)
    {
        float s = speed * dt;
        float yawRad = glm::radians(yaw);
        glm::vec3 fwd(std::sin(yawRad), 0.0f, std::cos(yawRad));
        glm::vec3 rht(std::cos(yawRad), 0.0f, -std::sin(yawRad));

        if (input.isKeyHeld(input::Key::W))
            target -= fwd * s;
        if (input.isKeyHeld(input::Key::S))
            target += fwd * s;
        if (input.isKeyHeld(input::Key::A))
            target -= rht * s;
        if (input.isKeyHeld(input::Key::D))
            target += rht * s;
        if (input.isKeyHeld(input::Key::Q))
            target.y -= s;
        if (input.isKeyHeld(input::Key::E))
            target.y += s;
    }
};

}  // namespace engine::core
