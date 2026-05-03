#pragma once

#include <cstdint>

#include "engine/math/Types.h"

namespace engine::platform
{
class VirtualJoystick;
}

namespace engine::ui
{

class UiDrawList;

// Visual configuration for `renderVirtualJoystick()`.  Colours are RGBA in
// the [0,1] range that `UiDrawList::drawRect` expects.  `baseAlpha` is a
// global multiplier applied to every layer's alpha so a single knob fades
// the whole widget in/out without changing per-layer hues.
struct VirtualJoystickRenderConfig
{
    math::Vec4 baseColor{0.5f, 0.5f, 0.5f, 0.6f};
    math::Vec4 stickColor{1.0f, 1.0f, 1.0f, 0.8f};
    math::Vec4 deadZoneColor{0.0f, 0.0f, 0.0f, 0.25f};
    float baseAlpha = 1.0f;
    float stickRadiusFactor = 0.30f;  // stick-circle radius as fraction of base radius
    bool drawDeadZone = true;
};

// Draws the virtual joystick as a translucent ring + stick using
// `UiDrawList::drawRect` with cornerRadius = half-size (the rounded-rect SDF
// shader produces a perfect circle when cornerRadius >= half-size).
//
// The function is a free function so `VirtualJoystick` itself stays free of
// UI dependencies and can live in the platform/android layer.
//
// `screenW` / `screenH` are the framebuffer pixel dimensions — the same
// values you pass to `UiRenderer::render(..., screenW, screenH)`.  They are
// used to convert the joystick's normalized config (0..1 of screen) into
// pixel coordinates.
void renderVirtualJoystick(const platform::VirtualJoystick& joy, UiDrawList& drawList,
                           uint16_t screenW, uint16_t screenH,
                           const VirtualJoystickRenderConfig& cfg = {});

}  // namespace engine::ui
