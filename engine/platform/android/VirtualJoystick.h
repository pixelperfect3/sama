#pragma once

#include "engine/math/Types.h"

namespace engine::platform
{

// Virtual on-screen joystick for mobile touch input.
// Renders a semi-transparent circle on the left side of the screen.
// Touch inside the circle produces a normalized direction vector.
//
// TODO: Implement rendering via UiRenderer and touch hit-testing with
// multi-touch. For now, only the positional logic is implemented.

struct VirtualJoystickConfig
{
    float centerX = 0.15f;      // normalized screen X (0-1)
    float centerY = 0.75f;      // normalized screen Y (0-1)
    float radiusScreen = 0.1f;  // radius as fraction of screen width
    float deadZone = 0.1f;      // inner dead zone (0-1 of radius)
    float opacity = 0.4f;       // visual opacity
};

class VirtualJoystick
{
public:
    // Returns normalized direction: x=[-1,1] right, y=[-1,1] up
    // Returns (0,0) when not touched or in dead zone
    math::Vec2 direction() const
    {
        return direction_;
    }
    bool isTouched() const
    {
        return touched_;
    }

    // Call each frame with current touch state.
    // touchX/touchY are in pixel coordinates.
    // screenWidth/Height needed to convert normalized config coords to pixels.
    void update(float touchX, float touchY, bool touchActive, float screenWidth,
                float screenHeight);

    void setConfig(const VirtualJoystickConfig& config)
    {
        config_ = config;
    }
    const VirtualJoystickConfig& config() const
    {
        return config_;
    }

private:
    VirtualJoystickConfig config_;
    math::Vec2 direction_{0.0f, 0.0f};
    bool touched_ = false;
};

}  // namespace engine::platform
