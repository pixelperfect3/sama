#include "engine/ui/VirtualJoystickRenderer.h"

#include <algorithm>

#include "engine/platform/android/VirtualJoystick.h"
#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

namespace
{

// Helper: emit a filled circle by drawing a square rounded-rect with corner
// radius >= half-size.  The UiRenderer's rounded-rect shader is an SDF that
// degenerates to a perfect circle in this case.
void drawCircle(UiDrawList& dl, float cx, float cy, float radius, math::Vec4 color)
{
    if (radius <= 0.f)
        return;

    const float diameter = radius * 2.f;
    dl.drawRect({cx - radius, cy - radius}, {diameter, diameter}, color, radius);
}

// Hollow ring by stacking an outer circle (deadZoneColor) and a punched-out
// inner one (zero alpha) on top.  We can't true-punch via UiDrawList, but a
// thin annulus reads fine if the overall alpha is low.
void drawRing(UiDrawList& dl, float cx, float cy, float outerRadius, float innerRadius,
              math::Vec4 color)
{
    if (outerRadius <= innerRadius || outerRadius <= 0.f)
        return;

    // Outer disk
    drawCircle(dl, cx, cy, outerRadius, color);
    // Inner disk uses a fully-transparent fill — gives us a visible band on
    // the SDF anti-aliased edge between the two circles.
    drawCircle(dl, cx, cy, innerRadius, math::Vec4{0.f, 0.f, 0.f, 0.f});
}

}  // namespace

void renderVirtualJoystick(const platform::VirtualJoystick& joy, UiDrawList& drawList,
                           uint16_t screenW, uint16_t screenH,
                           const VirtualJoystickRenderConfig& cfg)
{
    const auto& jc = joy.config();

    const float fbW = static_cast<float>(screenW);
    const float fbH = static_cast<float>(screenH);
    if (fbW <= 0.f || fbH <= 0.f)
        return;

    const float centerX = jc.centerX * fbW;
    const float centerY = jc.centerY * fbH;
    // Joystick logic uses screen WIDTH for the radius reference (see
    // VirtualJoystick::update) — mirror that here so the visual matches the
    // hit area exactly.
    const float radiusPx = jc.radiusScreen * fbW;

    const float alpha = std::clamp(cfg.baseAlpha, 0.f, 1.f);

    auto withAlpha = [alpha](math::Vec4 c)
    {
        c.w *= alpha;  // RGBA — alpha is .w under default glm swizzle settings
        return c;
    };

    // 1) Base disk
    drawCircle(drawList, centerX, centerY, radiusPx, withAlpha(cfg.baseColor));

    // 2) Optional dead-zone ring — only meaningful if a dead zone is set.
    if (cfg.drawDeadZone && jc.deadZone > 0.f)
    {
        const float dzPx = jc.deadZone * radiusPx;
        // A faint outline ring just inside the base disk.
        const float ringInner = std::max(0.f, dzPx - 1.0f);
        drawRing(drawList, centerX, centerY, dzPx, ringInner, withAlpha(cfg.deadZoneColor));
    }

    // 3) Stick circle — offset by joy.direction() within the radius.  Note
    //    that direction().y is up-positive, but screen Y grows downward, so
    //    we subtract for Y.
    const auto dir = joy.direction();
    const float stickRadius = radiusPx * std::max(0.05f, cfg.stickRadiusFactor);
    const float stickX = centerX + dir.x * radiusPx;
    const float stickY = centerY - dir.y * radiusPx;
    drawCircle(drawList, stickX, stickY, stickRadius, withAlpha(cfg.stickColor));
}

}  // namespace engine::ui
