#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "engine/input/ActionMap.h"
#include "engine/input/Key.h"

namespace engine::input
{

// A single touch/pointer contact.  id is stable within one gesture.
struct TouchPoint
{
    uint64_t id;
    float x;
    float y;
    enum class Phase : uint8_t
    {
        Began,  // contact started this frame
        Moved,  // contact moved or is stationary but still active
        Ended,  // contact lifted this frame
    } phase;
};

// Per-frame snapshot of all input state. Produced by InputSystem::update().
//
// Three-state key model (per frame):
//   isPressed  — key transitioned down this frame (first frame only)
//   isHeld     — key is currently down (true on the press frame and every subsequent frame)
//   isReleased — key transitioned up this frame
class InputState
{
public:
    // -----------------------------------------------------------------------
    // Key queries
    // -----------------------------------------------------------------------
    [[nodiscard]] bool isKeyPressed(Key k) const;
    [[nodiscard]] bool isKeyHeld(Key k) const;
    [[nodiscard]] bool isKeyReleased(Key k) const;

    // -----------------------------------------------------------------------
    // Action queries — resolve via ActionMap key bindings
    // -----------------------------------------------------------------------
    [[nodiscard]] bool isActionPressed(std::string_view action, const ActionMap& map) const;
    [[nodiscard]] bool isActionHeld(std::string_view action, const ActionMap& map) const;
    [[nodiscard]] bool isActionReleased(std::string_view action, const ActionMap& map) const;

    // -----------------------------------------------------------------------
    // Mouse button queries
    // -----------------------------------------------------------------------
    [[nodiscard]] bool isMouseButtonPressed(MouseButton b) const;
    [[nodiscard]] bool isMouseButtonHeld(MouseButton b) const;
    [[nodiscard]] bool isMouseButtonReleased(MouseButton b) const;

    // -----------------------------------------------------------------------
    // Mouse position and delta
    // -----------------------------------------------------------------------
    [[nodiscard]] double mouseX() const
    {
        return mouseX_;
    }
    [[nodiscard]] double mouseY() const
    {
        return mouseY_;
    }
    [[nodiscard]] double mouseDeltaX() const
    {
        return mouseDeltaX_;
    }
    [[nodiscard]] double mouseDeltaY() const
    {
        return mouseDeltaY_;
    }

    // -----------------------------------------------------------------------
    // Touch / pointer input
    // -----------------------------------------------------------------------
    // All active touches this frame (Began + Moved) plus touches that ended
    // this frame (Ended).  Between frames the list is rebuilt by InputSystem.
    [[nodiscard]] const std::vector<TouchPoint>& touches() const
    {
        return touches_;
    }

    // Convenience: find a touch by id.  Returns nullptr if not present.
    [[nodiscard]] const TouchPoint* touchById(uint64_t id) const;

    // -----------------------------------------------------------------------
    // Axis resolution — returns -1, 0, or +1
    // -----------------------------------------------------------------------
    [[nodiscard]] float axisValue(std::string_view axisName, const ActionMap& map) const;

    // -----------------------------------------------------------------------
    // Gyroscope / accelerometer
    // -----------------------------------------------------------------------
    struct GyroState
    {
        float pitchRate = 0.0f;  // rotation rate around X axis (radians/sec)
        float yawRate = 0.0f;    // rotation rate around Y axis (radians/sec)
        float rollRate = 0.0f;   // rotation rate around Z axis (radians/sec)
        float gravityX = 0.0f;   // gravity vector X (accelerometer, normalized)
        float gravityY = 0.0f;   // gravity vector Y
        float gravityZ = 0.0f;   // gravity vector Z
        bool available = false;  // true if the device has a gyroscope
    };

    [[nodiscard]] const GyroState& gyro() const
    {
        return gyro_;
    }

    // -----------------------------------------------------------------------
    // Internal: written by InputSystem; not part of the public contract
    // -----------------------------------------------------------------------
    static constexpr uint8_t kFlagPressed = 0x01;
    static constexpr uint8_t kFlagHeld = 0x02;
    static constexpr uint8_t kFlagReleased = 0x04;

    static constexpr size_t kKeyCount = static_cast<size_t>(Key::COUNT);
    static constexpr size_t kMouseButtonCount = static_cast<size_t>(MouseButton::COUNT);

    std::array<uint8_t, kKeyCount> keyFlags_{};
    std::array<uint8_t, kMouseButtonCount> mouseFlags_{};
    double mouseX_ = 0.0;
    double mouseY_ = 0.0;
    double mouseDeltaX_ = 0.0;
    double mouseDeltaY_ = 0.0;
    std::vector<TouchPoint> touches_;
    GyroState gyro_;
};

}  // namespace engine::input
