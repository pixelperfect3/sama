#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/ecs/Registry.h"
#include "engine/math/Frustum.h"
#include "engine/math/Types.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShadowRenderer.h"
#include "engine/rendering/systems/ShadowCullSystem.h"

using Catch::Approx;

// ---------------------------------------------------------------------------
// Headless bgfx fixture (Noop renderer)
// ---------------------------------------------------------------------------

struct HeadlessBgfx
{
    engine::rendering::Renderer renderer;

    HeadlessBgfx()
    {
        engine::rendering::RendererDesc desc{};
        desc.headless = true;
        desc.width = 1;
        desc.height = 1;
        REQUIRE(renderer.init(desc));
    }

    ~HeadlessBgfx()
    {
        renderer.shutdown();
    }

    HeadlessBgfx(const HeadlessBgfx&) = delete;
    HeadlessBgfx& operator=(const HeadlessBgfx&) = delete;
};

// ---------------------------------------------------------------------------
// ShadowRenderer — init / shutdown in headless (Noop) mode
// ---------------------------------------------------------------------------

TEST_CASE("ShadowRenderer: init does not crash in headless mode", "[shadow]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::ShadowRenderer shadowRenderer;
    engine::rendering::ShadowDesc desc{};
    desc.resolution = 2048;
    desc.cascadeCount = 1;

    // In Noop mode bgfx returns invalid handles for GPU resource creation.
    // init() may return false, but must not crash.
    REQUIRE_NOTHROW(shadowRenderer.init(desc));
    REQUIRE_NOTHROW(shadowRenderer.shutdown());
}

// ---------------------------------------------------------------------------
// ShadowRenderer — shadowMatrix maps expected coordinates to [0,1]^3 range
// ---------------------------------------------------------------------------

TEST_CASE("ShadowRenderer: shadowMatrix maps world point to shadow UV range", "[shadow]")
{
    // Build a simple orthographic light that looks down -Z at a scene around origin.
    // We don't need bgfx for this test — just GLM math.
    engine::rendering::ShadowRenderer shadowRenderer;
    engine::rendering::ShadowDesc desc{};
    desc.resolution = 2048;
    desc.cascadeCount = 1;

    // Manually set the internal matrices by calling beginCascade (needs bgfx) or
    // by testing shadowMatrix math directly.  We test the math in isolation:
    //
    //   lightView: camera at (0, 10, 0) looking toward -Y (down onto the scene).
    //   lightProj: ortho(-10,10, -10,10, 0.1, 20).
    //
    // A point at (0, 0, 0) should land at the centre of the shadow map (~0.5, 0.5).

    const engine::math::Mat4 lightView =
        glm::lookAt(engine::math::Vec3(0.0f, 10.0f, 0.0f),   // eye
                    engine::math::Vec3(0.0f, 0.0f, 0.0f),    // center
                    engine::math::Vec3(0.0f, 0.0f, -1.0f));  // up

    // GLM ortho with GLM_FORCE_DEPTH_ZERO_TO_ONE: depth in [0, 1].
    const engine::math::Mat4 lightProj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 20.0f);

    // Bypass bgfx by directly calling the matrix math.  We replicate
    // shadowMatrix() logic inline to avoid needing a live GPU context.
    const engine::math::Mat4 biasMatrix =
        glm::translate(engine::math::Mat4(1.0f), engine::math::Vec3(0.5f, 0.5f, 0.0f)) *
        glm::scale(engine::math::Mat4(1.0f), engine::math::Vec3(0.5f, 0.5f, 1.0f));

    const engine::math::Mat4 shadowMat = biasMatrix * lightProj * lightView;

    // Transform the world-space origin.
    engine::math::Vec4 shadowCoord = shadowMat * engine::math::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowCoord /= shadowCoord.w;

    // The origin should map to UV (0.5, 0.5) (centre of the shadow map).
    REQUIRE(shadowCoord.x == Approx(0.5f).margin(0.05f));
    REQUIRE(shadowCoord.y == Approx(0.5f).margin(0.05f));

    // Depth should be in [0, 1].
    REQUIRE(shadowCoord.z >= 0.0f);
    REQUIRE(shadowCoord.z <= 1.0f);
}

// ---------------------------------------------------------------------------
// loadShadowProgram — returns BGFX_INVALID_HANDLE in headless mode
// ---------------------------------------------------------------------------

TEST_CASE("loadShadowProgram: returns BGFX_INVALID_HANDLE in headless mode", "[shadow]")
{
    HeadlessBgfx bgfxCtx;

    bgfx::ProgramHandle program = engine::rendering::loadShadowProgram();
    REQUIRE(!bgfx::isValid(program));

    if (bgfx::isValid(program))
        bgfx::destroy(program);
}

// ---------------------------------------------------------------------------
// ShadowCullSystem — entity inside shadow frustum sets cascadeMask bit 0
// ---------------------------------------------------------------------------

TEST_CASE("ShadowCullSystem: entity inside shadow frustum sets cascadeMask bit 0", "[shadow]")
{
    HeadlessBgfx bgfx;
    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    // Build a unit cube centred at (0, 0, -5) — well inside the frustum below.
    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    const engine::ecs::EntityID entity = reg.createEntity();

    // Place the entity at world origin (cube bounds are -0.5..0.5 in each axis).
    engine::rendering::WorldTransformComponent wtc{engine::math::Mat4(1.0f)};
    reg.emplace<engine::rendering::WorldTransformComponent>(entity, wtc);
    reg.emplace<engine::rendering::MeshComponent>(entity, engine::rendering::MeshComponent{meshId});

    // Build a shadow frustum that fully encloses the origin.
    const engine::math::Mat4 lightView =
        glm::lookAt(engine::math::Vec3(0.0f, 10.0f, 0.0f), engine::math::Vec3(0.0f, 0.0f, 0.0f),
                    engine::math::Vec3(0.0f, 0.0f, -1.0f));
    const engine::math::Mat4 lightProj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 20.0f);
    const engine::math::Frustum shadowFrustum(lightProj * lightView);

    engine::rendering::ShadowCullSystem scs;
    scs.update(reg, res, shadowFrustum, 0);

    REQUIRE(reg.has<engine::rendering::ShadowVisibleTag>(entity));
    REQUIRE((reg.get<engine::rendering::ShadowVisibleTag>(entity)->cascadeMask & 0x01) != 0);

    res.destroyAll();
}

// ---------------------------------------------------------------------------
// ShadowCullSystem — entity outside shadow frustum clears cascadeMask bit 0
// ---------------------------------------------------------------------------

TEST_CASE("ShadowCullSystem: entity outside shadow frustum clears cascadeMask bit 0", "[shadow]")
{
    HeadlessBgfx bgfx;
    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    const engine::ecs::EntityID entity = reg.createEntity();

    // Place the entity far outside the light frustum (y=+1000).
    engine::math::Mat4 transform =
        glm::translate(engine::math::Mat4(1.0f), engine::math::Vec3(0.0f, 1000.0f, 0.0f));
    engine::rendering::WorldTransformComponent wtc{transform};
    reg.emplace<engine::rendering::WorldTransformComponent>(entity, wtc);
    reg.emplace<engine::rendering::MeshComponent>(entity, engine::rendering::MeshComponent{meshId});

    // Pre-tag the entity so we can verify it gets cleared.
    engine::rendering::ShadowVisibleTag preTag{};
    preTag.cascadeMask = 0x01;
    reg.emplace<engine::rendering::ShadowVisibleTag>(entity, preTag);

    // Small frustum around the origin — entity at y=1000 is outside.
    const engine::math::Mat4 lightView =
        glm::lookAt(engine::math::Vec3(0.0f, 10.0f, 0.0f), engine::math::Vec3(0.0f, 0.0f, 0.0f),
                    engine::math::Vec3(0.0f, 0.0f, -1.0f));
    const engine::math::Mat4 lightProj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 20.0f);
    const engine::math::Frustum shadowFrustum(lightProj * lightView);

    engine::rendering::ShadowCullSystem scs;
    scs.update(reg, res, shadowFrustum, 0);

    // Tag should be removed (cascadeMask became 0).
    REQUIRE_FALSE(reg.has<engine::rendering::ShadowVisibleTag>(entity));

    res.destroyAll();
}
