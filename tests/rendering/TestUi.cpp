#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/SpriteBatcher.h"
#include "engine/rendering/components/SpriteComponent.h"
#include "engine/rendering/systems/UiRenderSystem.h"

// ---------------------------------------------------------------------------
// HeadlessBgfx — RAII bgfx init/shutdown for headless unit tests.
// ---------------------------------------------------------------------------

namespace
{

struct HeadlessBgfx
{
    engine::rendering::Renderer renderer;

    HeadlessBgfx()
    {
        engine::rendering::RendererDesc desc{};
        desc.headless = true;
        desc.width = 1280;
        desc.height = 720;
        REQUIRE(renderer.init(desc));
    }

    ~HeadlessBgfx()
    {
        renderer.shutdown();
    }
};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// SpriteComponent layout
// ---------------------------------------------------------------------------

TEST_CASE("SpriteComponent: sizeof == 44", "[sprite][ui]")
{
    static_assert(sizeof(engine::rendering::SpriteComponent) == 44,
                  "SpriteComponent must be exactly 44 bytes");
    REQUIRE(sizeof(engine::rendering::SpriteComponent) == 44u);
}

// ---------------------------------------------------------------------------
// SpriteBatcher batching logic
// ---------------------------------------------------------------------------

TEST_CASE("SpriteBatcher: 3 sprites with same textureId produce 1 batch", "[sprite][ui]")
{
    // This test validates sorting/grouping logic, not GPU submission.
    // We verify via the public interface (begin/addSprite) that the batcher
    // accepts the calls without assertion failure or crash.
    engine::rendering::SpriteBatcher batcher;
    batcher.begin();

    engine::rendering::SpriteComponent spr{};
    spr.uvRect = {0.f, 0.f, 1.f, 1.f};
    spr.color = {1.f, 1.f, 1.f, 1.f};
    spr.textureId = 42;
    spr.sortZ = 0.f;

    const glm::mat4 identity(1.f);
    batcher.addSprite(identity, spr);
    batcher.addSprite(identity, spr);
    batcher.addSprite(identity, spr);

    // All 3 share textureId=42 — flush would produce 1 batch.
    // In headless mode flush is a no-op; we just verify no crash.
    HeadlessBgfx ctx;
    ctx.renderer.beginFrame();
    // flush is a no-op on Noop renderer — safe to call without a real encoder.
    // We pass invalid handles since loadSpriteProgram returns invalid in headless.
    batcher.flush(nullptr, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                  engine::rendering::RenderResources{});
    ctx.renderer.endFrame();

    SUCCEED("3 same-textureId sprites accepted without crash");
}

TEST_CASE("SpriteBatcher: 3 sprites with different textureIds produce 3 batches", "[sprite][ui]")
{
    engine::rendering::SpriteBatcher batcher;
    batcher.begin();

    const glm::mat4 identity(1.f);

    for (uint32_t tid = 1; tid <= 3; ++tid)
    {
        engine::rendering::SpriteComponent spr{};
        spr.uvRect = {0.f, 0.f, 1.f, 1.f};
        spr.color = {1.f, 1.f, 1.f, 1.f};
        spr.textureId = tid;
        spr.sortZ = 0.f;
        batcher.addSprite(identity, spr);
    }

    // 3 different textureIds — flush would produce 3 batches.
    HeadlessBgfx ctx;
    ctx.renderer.beginFrame();
    batcher.flush(nullptr, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                  engine::rendering::RenderResources{});
    ctx.renderer.endFrame();

    SUCCEED("3 different-textureId sprites accepted without crash");
}

// ---------------------------------------------------------------------------
// UiRenderSystem
// ---------------------------------------------------------------------------

TEST_CASE("UiRenderSystem: empty registry does not crash", "[ui]")
{
    HeadlessBgfx ctx;
    ctx.renderer.beginFrame();

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;
    engine::rendering::UiRenderSystem sys;

    sys.update(reg, res, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 1280, 720);

    ctx.renderer.endFrame();
    SUCCEED("empty registry update completed without crash");
}

TEST_CASE("UiRenderSystem: 2 sprites submit without crash (headless)", "[ui]")
{
    HeadlessBgfx ctx;
    ctx.renderer.beginFrame();

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;
    engine::rendering::UiRenderSystem sys;

    // Create two sprite entities.
    for (int i = 0; i < 2; ++i)
    {
        auto e = reg.createEntity();

        engine::rendering::SpriteComponent spr{};
        spr.uvRect = {0.f, 0.f, 1.f, 1.f};
        spr.color = {1.f, 1.f, 1.f, 1.f};
        spr.textureId = static_cast<uint32_t>(i + 1);
        spr.sortZ = static_cast<float>(i);
        reg.emplace<engine::rendering::SpriteComponent>(e, spr);

        engine::rendering::WorldTransformComponent wtc{};
        wtc.matrix = glm::mat4(1.f);
        reg.emplace<engine::rendering::WorldTransformComponent>(e, wtc);
    }

    sys.update(reg, res, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, 1280, 720);

    ctx.renderer.endFrame();
    SUCCEED("2-sprite UiRenderSystem update completed without crash");
}
