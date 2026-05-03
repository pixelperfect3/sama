#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/platform/android/VirtualJoystick.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/VirtualJoystickRenderer.h"

using Catch::Matchers::WithinAbs;
using engine::platform::VirtualJoystick;
using engine::platform::VirtualJoystickConfig;
using engine::ui::renderVirtualJoystick;
using engine::ui::UiDrawCmd;
using engine::ui::UiDrawList;
using engine::ui::VirtualJoystickRenderConfig;

namespace
{
constexpr uint16_t kScreenW = 1080;
constexpr uint16_t kScreenH = 1920;
constexpr float kEps = 0.5f;  // pixel tolerance for circle-square geometry

// Count `Rect` commands in the draw list — anything the renderer emits is a
// rounded rect.  Skip Text/TexturedRect just in case.
size_t rectCount(const UiDrawList& dl)
{
    size_t n = 0;
    for (const auto& cmd : dl.commands())
        if (cmd.type == UiDrawCmd::Rect)
            ++n;
    return n;
}

}  // namespace

TEST_CASE("VirtualJoystickRenderer: emits base + stick (no dead-zone) when dz=0",
          "[ui][joystick][render]")
{
    VirtualJoystick joy;
    VirtualJoystickConfig cfg;
    cfg.deadZone = 0.f;
    joy.setConfig(cfg);

    UiDrawList dl;
    renderVirtualJoystick(joy, dl, kScreenW, kScreenH);

    // Base disk + stick disk = 2 rects.  Dead-zone ring suppressed.
    CHECK(rectCount(dl) == 2);
}

TEST_CASE("VirtualJoystickRenderer: emits base + dead-zone-ring + stick by default",
          "[ui][joystick][render]")
{
    VirtualJoystick joy;  // default config has deadZone = 0.1
    UiDrawList dl;
    renderVirtualJoystick(joy, dl, kScreenW, kScreenH);

    // base disk (1) + dead-zone ring (outer + inner punch = 2) + stick disk (1)
    CHECK(rectCount(dl) == 4);
}

TEST_CASE("VirtualJoystickRenderer: drawDeadZone=false drops the ring", "[ui][joystick][render]")
{
    VirtualJoystick joy;
    UiDrawList dl;
    VirtualJoystickRenderConfig rcfg;
    rcfg.drawDeadZone = false;
    renderVirtualJoystick(joy, dl, kScreenW, kScreenH, rcfg);

    CHECK(rectCount(dl) == 2);
}

TEST_CASE("VirtualJoystickRenderer: zero screen dims is a no-op", "[ui][joystick][render]")
{
    VirtualJoystick joy;
    UiDrawList dl;
    renderVirtualJoystick(joy, dl, 0, 0);
    CHECK(dl.commands().empty());
}

TEST_CASE("VirtualJoystickRenderer: base disk centred at config.center, sized to config.radius",
          "[ui][joystick][render]")
{
    VirtualJoystick joy;
    VirtualJoystickConfig cfg;
    cfg.centerX = 0.20f;
    cfg.centerY = 0.85f;
    cfg.radiusScreen = 0.10f;
    cfg.deadZone = 0.f;  // exclude the ring for this test
    joy.setConfig(cfg);

    UiDrawList dl;
    renderVirtualJoystick(joy, dl, kScreenW, kScreenH);

    REQUIRE(rectCount(dl) >= 1);
    const auto& base = dl.commands()[0];

    const float expectedRadius = cfg.radiusScreen * kScreenW;  // 108 px
    const float expectedCx = cfg.centerX * kScreenW;
    const float expectedCy = cfg.centerY * kScreenH;

    CHECK_THAT(base.size.x, WithinAbs(expectedRadius * 2.f, kEps));
    CHECK_THAT(base.size.y, WithinAbs(expectedRadius * 2.f, kEps));
    CHECK_THAT(base.position.x + base.size.x * 0.5f, WithinAbs(expectedCx, kEps));
    CHECK_THAT(base.position.y + base.size.y * 0.5f, WithinAbs(expectedCy, kEps));
    // cornerRadius = halfSize → SDF circle.
    CHECK_THAT(base.cornerRadius, WithinAbs(expectedRadius, kEps));
}

TEST_CASE("VirtualJoystickRenderer: stick offsets in screen pixels by direction",
          "[ui][joystick][render]")
{
    // Build a joystick, drive it with a known input so direction becomes
    // (1, 0) (full right), then render and confirm the *last* rect (the
    // stick disk) is offset to the right by `radiusPx` from centre.
    VirtualJoystick joy;
    VirtualJoystickConfig cfg;
    cfg.centerX = 0.5f;
    cfg.centerY = 0.5f;
    cfg.radiusScreen = 0.10f;
    cfg.deadZone = 0.f;
    joy.setConfig(cfg);

    const float fbW = static_cast<float>(kScreenW);
    const float fbH = static_cast<float>(kScreenH);
    const float centerPx = cfg.centerX * fbW;
    const float centerPy = cfg.centerY * fbH;
    const float radiusPx = cfg.radiusScreen * fbW;
    joy.update(centerPx + radiusPx, centerPy, true, fbW, fbH);

    REQUIRE_THAT(joy.direction().x, WithinAbs(1.f, 0.05f));

    UiDrawList dl;
    renderVirtualJoystick(joy, dl, kScreenW, kScreenH);

    // Last rect emitted is the stick disk by construction.
    REQUIRE(!dl.commands().empty());
    const auto& stick = dl.commands().back();

    const float stickCx = stick.position.x + stick.size.x * 0.5f;
    const float stickCy = stick.position.y + stick.size.y * 0.5f;

    CHECK_THAT(stickCx, WithinAbs(centerPx + radiusPx, kEps));
    CHECK_THAT(stickCy, WithinAbs(centerPy, kEps));  // pure horizontal — no Y move
}

TEST_CASE("VirtualJoystickRenderer: direction.y is up-positive (subtract from screen Y)",
          "[ui][joystick][render]")
{
    // Touch ABOVE centre (smaller pixel Y) → direction.y > 0.  Renderer
    // must place the stick disk above centre too (smaller pixel Y).
    VirtualJoystick joy;
    VirtualJoystickConfig cfg;
    cfg.centerX = 0.5f;
    cfg.centerY = 0.5f;
    cfg.radiusScreen = 0.10f;
    cfg.deadZone = 0.f;
    joy.setConfig(cfg);

    const float fbW = static_cast<float>(kScreenW);
    const float fbH = static_cast<float>(kScreenH);
    const float centerPx = cfg.centerX * fbW;
    const float centerPy = cfg.centerY * fbH;
    const float radiusPx = cfg.radiusScreen * fbW;
    joy.update(centerPx, centerPy - radiusPx, true, fbW, fbH);

    REQUIRE(joy.direction().y > 0.5f);

    UiDrawList dl;
    renderVirtualJoystick(joy, dl, kScreenW, kScreenH);
    const auto& stick = dl.commands().back();
    const float stickCy = stick.position.y + stick.size.y * 0.5f;

    CHECK(stickCy < centerPy);  // visually above the centre on screen
}

TEST_CASE("VirtualJoystickRenderer: baseAlpha multiplies layer alphas", "[ui][joystick][render]")
{
    VirtualJoystick joy;
    UiDrawList dl;
    VirtualJoystickRenderConfig rcfg;
    rcfg.baseColor = {1.f, 1.f, 1.f, 0.8f};
    rcfg.stickColor = {1.f, 1.f, 1.f, 1.0f};
    rcfg.baseAlpha = 0.5f;
    rcfg.drawDeadZone = false;
    renderVirtualJoystick(joy, dl, kScreenW, kScreenH, rcfg);

    REQUIRE(dl.commands().size() == 2);
    // Base disk alpha = 0.8 * 0.5 = 0.4
    CHECK_THAT(dl.commands()[0].color.w, WithinAbs(0.4f, 1e-4f));
    // Stick disk alpha = 1.0 * 0.5 = 0.5
    CHECK_THAT(dl.commands()[1].color.w, WithinAbs(0.5f, 1e-4f));
}
