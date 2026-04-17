#include "engine/platform/android/VirtualJoystick.h"

#include <cmath>

namespace engine::platform
{

void VirtualJoystick::update(float touchX, float touchY, bool touchActive, float screenWidth,
                             float screenHeight)
{
    if (!touchActive)
    {
        touched_ = false;
        direction_ = math::Vec2{0.0f, 0.0f};
        return;
    }

    // Convert normalized config center to pixel coordinates
    float centerPx = config_.centerX * screenWidth;
    float centerPy = config_.centerY * screenHeight;
    float radiusPx = config_.radiusScreen * screenWidth;

    // Vector from center to touch position
    float dx = touchX - centerPx;
    float dy = -(touchY - centerPy);  // flip Y so up is positive

    float dist = std::sqrt(dx * dx + dy * dy);

    // Outside the joystick radius — not touching
    // We use a generous 1.5x radius for activation so slight overshoots
    // still register, but clamp the output to unit length.
    if (dist > radiusPx * 1.5f)
    {
        touched_ = false;
        direction_ = math::Vec2{0.0f, 0.0f};
        return;
    }

    touched_ = true;

    // Inside dead zone
    float deadZonePx = config_.deadZone * radiusPx;
    if (dist <= deadZonePx)
    {
        direction_ = math::Vec2{0.0f, 0.0f};
        return;
    }

    // Normalize direction
    float nx = dx / dist;
    float ny = dy / dist;

    // Remap distance from [deadZone, radius] to [0, 1] and clamp
    float effectiveDist = (dist - deadZonePx) / (radiusPx - deadZonePx);
    if (effectiveDist > 1.0f)
        effectiveDist = 1.0f;

    direction_ = math::Vec2{nx * effectiveDist, ny * effectiveDist};
}

}  // namespace engine::platform
