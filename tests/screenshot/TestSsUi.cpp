// UI sprite screenshot test.
// Scene: 2D orthographic view (0,0,320,240). Three colored quads (red, green, blue)
// as SpriteComponents via UiRenderSystem + SpriteBatcher, rendered to kViewUi.
// textureId=0 means "white" — the tint color from SpriteComponent is the output color.

#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/ViewIds.h"
#include "engine/rendering/components/SpriteComponent.h"
#include "engine/rendering/systems/UiRenderSystem.h"

TEST_CASE("screenshot: UI sprites", "[screenshot]")
{
    engine::screenshot::ScreenshotFixture fx;
    engine::rendering::ShaderUniforms uniforms;
    uniforms.init();

    bgfx::ProgramHandle spriteProgram = engine::rendering::loadSpriteProgram();

    // Set up the UI view target
    engine::rendering::RenderPass(engine::rendering::kViewUi)
        .framebuffer(fx.captureFb())
        .rect(0, 0, fx.width(), fx.height())
        .clearColorAndDepth(0x202020ff);

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;
    res.setWhiteTexture(fx.whiteTex());
    engine::rendering::UiRenderSystem uiSys;

    // Helper to create a quad entity with a colored sprite
    auto makeSprite =
        [&](float x, float y, float w, float h, float r, float g, float b, float sortZ)
    {
        auto e = reg.createEntity();

        engine::rendering::SpriteComponent spr{};
        spr.uvRect = {0.0f, 0.0f, 1.0f, 1.0f};
        spr.color = {r, g, b, 1.0f};
        spr.textureId = 0;  // 0 = white texture (tinted by color)
        spr.sortZ = sortZ;
        reg.emplace<engine::rendering::SpriteComponent>(e, spr);

        // Place sprite via world transform: translate to center, scale to size
        auto model =
            glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x + w * 0.5f, y + h * 0.5f, 0.0f)),
                       glm::vec3(w, h, 1.0f));
        engine::rendering::WorldTransformComponent wtc{};
        wtc.matrix = model;
        reg.emplace<engine::rendering::WorldTransformComponent>(e, wtc);

        return e;
    };

    // Three colored quads: red, green, blue — spread across the 320x240 canvas
    makeSprite(10.0f, 80.0f, 80.0f, 80.0f, 1.0f, 0.0f, 0.0f, 0.0f);   // red
    makeSprite(120.0f, 80.0f, 80.0f, 80.0f, 0.0f, 1.0f, 0.0f, 1.0f);  // green
    makeSprite(230.0f, 80.0f, 80.0f, 80.0f, 0.0f, 0.0f, 1.0f, 2.0f);  // blue

    // UiRenderSystem resets kViewUi clear to BGFX_CLEAR_NONE (UI renders on top
    // of the 3D scene in production).  For the screenshot test we want a solid
    // background, so restore the clear and framebuffer after the system runs.
    uiSys.update(reg, res, spriteProgram, uniforms.s_albedo, static_cast<uint16_t>(fx.width()),
                 static_cast<uint16_t>(fx.height()));
    engine::rendering::RenderPass(engine::rendering::kViewUi)
        .framebuffer(fx.captureFb())
        .clearColorAndDepth(0x202020ff);

    auto pixels = fx.captureFrame();

    if (bgfx::isValid(spriteProgram))
        bgfx::destroy(spriteProgram);
    res.destroyAll();
    uniforms.destroy();

    REQUIRE(
        engine::screenshot::compareOrUpdateGolden("ui_sprites", pixels, fx.width(), fx.height()));
}
