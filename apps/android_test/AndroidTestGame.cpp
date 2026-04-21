// Android Test — cross-platform input test game.
//
// Renders a colored background that responds to input:
//   - Touch/click: changes hue based on touch X position
//   - Drag: leaves a trail of colored dots showing touch path
//   - Gyro tilt: shifts the background brightness based on device tilt
//   - Multi-touch: each finger shows a separate colored dot
//   - Keyboard: Space resets, Escape quits
//
// Also displays debug text showing input state (touch count, gyro values,
// mouse position, active keys).
//
// Desktop:  build/android_test  (mouse + keyboard simulate touch/gyro)
// Android:  linked into libsama_android.so via samaCreateGame()

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/game/GameRunner.h"
#include "engine/game/IGame.h"
#include "engine/input/InputState.h"
#include "engine/input/Key.h"

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::game;
using namespace engine::input;
using namespace engine::rendering;

namespace
{

// Convert HSV (h in [0,360), s/v in [0,1]) to a packed RGBA uint32_t.
uint32_t hsvToRgba(float h, float s, float v)
{
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r = 0, g = 0, b = 0;
    if (h < 60)
    {
        r = c;
        g = x;
    }
    else if (h < 120)
    {
        r = x;
        g = c;
    }
    else if (h < 180)
    {
        g = c;
        b = x;
    }
    else if (h < 240)
    {
        g = x;
        b = c;
    }
    else if (h < 300)
    {
        r = x;
        b = c;
    }
    else
    {
        r = c;
        b = x;
    }

    auto toU8 = [](float f) -> uint8_t
    { return static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };

    return (toU8(r + m) << 24) | (toU8(g + m) << 16) | (toU8(b + m) << 8) | 0xFF;
}

}  // namespace

class AndroidTestGame : public IGame
{
public:
    void onInit(Engine& /*engine*/, Registry& /*registry*/) override
    {
        elapsed_ = 0.0f;
        hue_ = 200.0f;
        brightness_ = 0.4f;
    }

    void onUpdate(Engine& engine, Registry& /*registry*/, float dt) override
    {
        elapsed_ += dt;
        frameCount_++;
        const auto& input = engine.inputState();
        const float fbW = static_cast<float>(engine.fbWidth());
        const float fbH = static_cast<float>(engine.fbHeight());

        // --- Touch / mouse input ---
        // On Android: real touch events. On desktop: mouse emulates first touch.
        if (input.isMouseButtonHeld(MouseButton::Left))
        {
            // Map mouse X to hue (0..360)
            float normX = static_cast<float>(input.mouseX()) / std::max(fbW, 1.0f);
            hue_ = normX * 360.0f;

            // Track touch trail
            TouchDot dot;
            dot.x = static_cast<float>(input.mouseX());
            dot.y = static_cast<float>(input.mouseY());
            dot.hue = hue_;
            dot.age = 0.0f;
            touchTrail_.push_back(dot);
        }

        // Multi-touch: each touch gets a dot
        for (const auto& touch : input.touches())
        {
            if (touch.phase == TouchPoint::Phase::Began || touch.phase == TouchPoint::Phase::Moved)
            {
                TouchDot dot;
                dot.x = touch.x;
                dot.y = touch.y;
                dot.hue = std::fmod(static_cast<float>(touch.id) * 60.0f, 360.0f);
                dot.age = 0.0f;
                touchTrail_.push_back(dot);
            }
        }

        // Age and prune trail dots (fade over 2 seconds)
        for (auto& dot : touchTrail_)
            dot.age += dt;
        touchTrail_.erase(std::remove_if(touchTrail_.begin(), touchTrail_.end(),
                                         [](const TouchDot& d) { return d.age > 2.0f; }),
                          touchTrail_.end());

        // --- Gyro input ---
        const auto& gyro = input.gyro();
        if (gyro.available)
        {
            // Tilt forward/back adjusts brightness
            brightness_ = std::clamp(0.4f + gyro.gravityZ * 0.3f, 0.1f, 0.8f);

            // Tilt left/right shifts hue
            hue_ += gyro.yawRate * dt * 60.0f;
            if (hue_ < 0.0f)
                hue_ += 360.0f;
            if (hue_ >= 360.0f)
                hue_ -= 360.0f;
        }

        // --- Keyboard ---
        if (input.isKeyPressed(Key::Space))
        {
            // Reset
            hue_ = 200.0f;
            brightness_ = 0.4f;
            touchTrail_.clear();
        }

        // Slow auto-cycle when idle (no touch)
        if (!input.isMouseButtonHeld(MouseButton::Left) && input.touches().empty())
        {
            hue_ = std::fmod(hue_ + dt * 10.0f, 360.0f);
        }

        // --- Render ---
        // Use view 0 for maximum compatibility (works even without full
        // renderer setup on Android where shaders are stubbed).
        uint32_t bgColor = hsvToRgba(hue_, 0.6f, brightness_);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, bgColor, 1.0f, 0);
        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(engine.fbWidth()),
                          static_cast<uint16_t>(engine.fbHeight()));
        bgfx::touch(0);

        // --- Debug text overlay ---
        bgfx::dbgTextClear();
        int row = 1;

        bgfx::dbgTextPrintf(1, row++, 0x0f, "Android Test  |  %.1f fps  |  %.3f ms",
                            dt > 0 ? 1.0f / dt : 0.0f, dt * 1000.0f);
        bgfx::dbgTextPrintf(1, row++, 0x0f, "Screen: %ux%u", engine.fbWidth(), engine.fbHeight());
        row++;

        // Mouse / touch position
        bgfx::dbgTextPrintf(1, row++, 0x07, "Mouse: (%.0f, %.0f)  %s", input.mouseX(),
                            input.mouseY(),
                            input.isMouseButtonHeld(MouseButton::Left) ? "[LEFT]" : "");

        // Touch points
        bgfx::dbgTextPrintf(1, row++, 0x07, "Touches: %zu active", input.touches().size());
        int touchRow = 0;
        for (const auto& touch : input.touches())
        {
            const char* phase = "?";
            if (touch.phase == TouchPoint::Phase::Began)
                phase = "Began";
            else if (touch.phase == TouchPoint::Phase::Moved)
                phase = "Moved";
            else if (touch.phase == TouchPoint::Phase::Ended)
                phase = "Ended";
            bgfx::dbgTextPrintf(3, row++, 0x06, "  [%llu] (%.0f, %.0f) %s",
                                static_cast<unsigned long long>(touch.id), touch.x, touch.y, phase);
            if (++touchRow >= 5)
                break;  // limit display
        }
        row++;

        // Gyro
        if (gyro.available)
        {
            bgfx::dbgTextPrintf(1, row++, 0x0a, "Gyro: pitch=%.2f  yaw=%.2f  roll=%.2f",
                                gyro.pitchRate, gyro.yawRate, gyro.rollRate);
            bgfx::dbgTextPrintf(1, row++, 0x0a, "Gravity: (%.2f, %.2f, %.2f)", gyro.gravityX,
                                gyro.gravityY, gyro.gravityZ);
        }
        else
        {
            bgfx::dbgTextPrintf(1, row++, 0x08, "Gyro: not available");
        }
        row++;

        // Trail info
        bgfx::dbgTextPrintf(1, row++, 0x07, "Trail dots: %zu", touchTrail_.size());
        bgfx::dbgTextPrintf(1, row++, 0x07, "Hue: %.0f  Brightness: %.2f", hue_, brightness_);
        row++;

        // Controls
        bgfx::dbgTextPrintf(1, row++, 0x06, "--- Controls ---");
        bgfx::dbgTextPrintf(1, row++, 0x06, "Touch/Click: change hue by X position");
        bgfx::dbgTextPrintf(1, row++, 0x06, "Drag: draw colored trail");
        bgfx::dbgTextPrintf(1, row++, 0x06, "Gyro tilt: adjust brightness + hue");
        bgfx::dbgTextPrintf(1, row++, 0x06, "Space: reset  |  Escape: quit");
    }

private:
    float elapsed_ = 0.0f;
    float hue_ = 200.0f;
    float brightness_ = 0.4f;
    int frameCount_ = 0;

    struct TouchDot
    {
        float x = 0.0f;
        float y = 0.0f;
        float hue = 0.0f;
        float age = 0.0f;
    };
    std::vector<TouchDot> touchTrail_;
};

// ---------------------------------------------------------------------------
// Entry points — desktop and Android
// ---------------------------------------------------------------------------

#ifdef __ANDROID__

engine::game::IGame* samaCreateGame()
{
    return new AndroidTestGame();
}

#else

int main()
{
    AndroidTestGame game;
    GameRunner runner(game);

    EngineDesc desc;
    desc.windowTitle = "Android Test — Input";
    desc.windowWidth = 800;
    desc.windowHeight = 600;
    return runner.run(desc);
}

#endif
