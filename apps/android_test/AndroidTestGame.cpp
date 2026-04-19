// Android Test — minimal IGame that renders a flat colored screen.
//
// This is the simplest possible Sama game, used to verify the Android
// build pipeline end-to-end. It renders a solid color background that
// slowly cycles through hues so you can confirm the frame loop is running.
//
// Desktop:  build/android_test
// Android:  linked into libsama_android.so via samaCreateGame()

#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdint>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/game/GameRunner.h"
#include "engine/game/IGame.h"
#include "engine/rendering/ViewIds.h"

using namespace engine::core;
using namespace engine::ecs;
using namespace engine::game;
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

    auto toU8 = [](float f) -> uint8_t { return static_cast<uint8_t>((f + 0.0f) * 255.0f + 0.5f); };

    return (toU8(r + m) << 24) | (toU8(g + m) << 16) | (toU8(b + m) << 8) | 0xFF;
}

}  // namespace

class AndroidTestGame : public IGame
{
public:
    void onInit(Engine& /*engine*/, Registry& /*registry*/) override
    {
        elapsed_ = 0.0f;
    }

    void onUpdate(Engine& engine, Registry& /*registry*/, float dt) override
    {
        elapsed_ += dt;

        // Cycle hue over 10 seconds.
        float hue = std::fmod(elapsed_ * 36.0f, 360.0f);
        uint32_t color = hsvToRgba(hue, 0.6f, 0.4f);

        bgfx::setViewClear(kViewOpaque, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, color, 1.0f, 0);
        bgfx::setViewRect(kViewOpaque, 0, 0, static_cast<uint16_t>(engine.fbWidth()),
                          static_cast<uint16_t>(engine.fbHeight()));
        bgfx::touch(kViewOpaque);
    }

private:
    float elapsed_ = 0.0f;
};

// ---------------------------------------------------------------------------
// Entry points — desktop and Android
// ---------------------------------------------------------------------------

#ifdef __ANDROID__

// Android: factory function called by AndroidApp.cpp
engine::game::IGame* samaCreateGame()
{
    return new AndroidTestGame();
}

#else

// Desktop: standard main()
int main()
{
    AndroidTestGame game;
    GameRunner runner(game);

    EngineDesc desc;
    desc.windowTitle = "Android Test";
    desc.windowWidth = 640;
    desc.windowHeight = 480;
    return runner.run(desc);
}

#endif
