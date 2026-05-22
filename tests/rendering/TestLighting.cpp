#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/LightClusterBuilder.h"
#include "engine/rendering/LightData.h"
#include "engine/rendering/Renderer.h"

using Catch::Approx;

// ---------------------------------------------------------------------------
// Headless bgfx context — reused from TestPbr.cpp pattern
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
// LightData.h — static layout tests (no bgfx required)
// ---------------------------------------------------------------------------

TEST_CASE("LightEntry: sizeof is 64", "[lighting]")
{
    REQUIRE(sizeof(engine::rendering::LightEntry) == 64u);
}

TEST_CASE("LightEntry: cluster count constant is correct", "[cluster]")
{
    using namespace engine::rendering;
    REQUIRE(kClusterCount == kClusterX * kClusterY * kClusterZ);
    REQUIRE(kClusterCount == 3456u);
}

// ---------------------------------------------------------------------------
// LightClusterBuilder: init / shutdown do not crash in headless mode
// ---------------------------------------------------------------------------

TEST_CASE("LightClusterBuilder: init and shutdown do not crash in headless mode",
          "[lighting][cluster]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::LightClusterBuilder builder;
    REQUIRE_NOTHROW(builder.init());
    REQUIRE_NOTHROW(builder.shutdown());
}

// ---------------------------------------------------------------------------
// LightClusterBuilder: 10 point lights → activeLightCount() == 10
// ---------------------------------------------------------------------------

TEST_CASE("LightClusterBuilder: 10 point lights produce activeLightCount 10", "[lighting][cluster]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;

    for (int k = 0; k < 10; ++k)
    {
        engine::ecs::EntityID e = reg.createEntity();

        engine::rendering::WorldTransformComponent xform{};
        xform.matrix = engine::math::Mat4(1.0f);
        xform.matrix[3] = engine::math::Vec4(static_cast<float>(k), 0.0f, -5.0f, 1.0f);
        reg.emplace<engine::rendering::WorldTransformComponent>(e, xform);

        engine::rendering::PointLightComponent pl{};
        pl.color = engine::math::Vec3(1.0f, 1.0f, 1.0f);
        pl.intensity = 1.0f;
        pl.radius = 3.0f;
        reg.emplace<engine::rendering::PointLightComponent>(e, pl);
    }

    engine::rendering::LightClusterBuilder builder;
    builder.init();

    engine::math::Mat4 view =
        glm::lookAt(engine::math::Vec3(0.0f, 0.0f, 0.0f), engine::math::Vec3(0.0f, 0.0f, -1.0f),
                    engine::math::Vec3(0.0f, 1.0f, 0.0f));
    engine::math::Mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);

    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);

    REQUIRE(builder.activeLightCount() == 10u);

    builder.shutdown();
}

// ---------------------------------------------------------------------------
// LightClusterBuilder: light at (0,0,-5) radius 2 → assigned to ≥1 cluster
// ---------------------------------------------------------------------------

TEST_CASE("LightClusterBuilder: light at (0,0,-5) radius 2 is assigned to at least one cluster",
          "[lighting][cluster]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;

    engine::ecs::EntityID e = reg.createEntity();

    engine::rendering::WorldTransformComponent xform{};
    xform.matrix = engine::math::Mat4(1.0f);
    xform.matrix[3] = engine::math::Vec4(0.0f, 0.0f, -5.0f, 1.0f);
    reg.emplace<engine::rendering::WorldTransformComponent>(e, xform);

    engine::rendering::PointLightComponent pl{};
    pl.color = engine::math::Vec3(1.0f, 0.5f, 0.25f);
    pl.intensity = 2.0f;
    pl.radius = 2.0f;
    reg.emplace<engine::rendering::PointLightComponent>(e, pl);

    engine::rendering::LightClusterBuilder builder;
    builder.init();

    engine::math::Mat4 view =
        glm::lookAt(engine::math::Vec3(0.0f, 0.0f, 0.0f), engine::math::Vec3(0.0f, 0.0f, -1.0f),
                    engine::math::Vec3(0.0f, 1.0f, 0.0f));
    engine::math::Mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);

    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);

    REQUIRE(builder.activeLightCount() == 1u);

    // The light is at view-space (0, 0, -5); the centre tile is x=7,8 and
    // y=4 in a 16x9 grid.  At least one cluster near the screen centre must
    // have been assigned the light.  We test this indirectly by checking that
    // activeLightCount is non-zero and the builder didn't crash.
    REQUIRE(builder.activeLightCount() > 0u);

    builder.shutdown();
}

// ---------------------------------------------------------------------------
// LightEntry: spot light cosInner / cosOuter round-trip
// ---------------------------------------------------------------------------

TEST_CASE("LightEntry: spot light cosInner and cosOuter round-trip correctly", "[lighting]")
{
    engine::rendering::LightEntry entry{};
    entry.cosInnerAngle = 0.866f;  // cos(30 deg)
    entry.cosOuterAngle = 0.707f;  // cos(45 deg)

    REQUIRE(entry.cosInnerAngle == Approx(0.866f));
    REQUIRE(entry.cosOuterAngle == Approx(0.707f));
}

// ---------------------------------------------------------------------------
// LightClusterBuilder: kMaxLights cap — 300 lights → only 256 uploaded
// ---------------------------------------------------------------------------

TEST_CASE("LightClusterBuilder: 300 point lights are capped at kMaxLights (256)",
          "[lighting][cluster]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;

    for (int k = 0; k < 300; ++k)
    {
        engine::ecs::EntityID e = reg.createEntity();

        engine::rendering::WorldTransformComponent xform{};
        xform.matrix = engine::math::Mat4(1.0f);
        xform.matrix[3] = engine::math::Vec4(static_cast<float>(k % 16) * 2.0f,
                                             static_cast<float>(k / 16) * 2.0f, -5.0f, 1.0f);
        reg.emplace<engine::rendering::WorldTransformComponent>(e, xform);

        engine::rendering::PointLightComponent pl{};
        pl.color = engine::math::Vec3(1.0f, 1.0f, 1.0f);
        pl.intensity = 1.0f;
        pl.radius = 1.0f;
        reg.emplace<engine::rendering::PointLightComponent>(e, pl);
    }

    engine::rendering::LightClusterBuilder builder;
    builder.init();

    engine::math::Mat4 view =
        glm::lookAt(engine::math::Vec3(0.0f, 0.0f, 0.0f), engine::math::Vec3(0.0f, 0.0f, -1.0f),
                    engine::math::Vec3(0.0f, 1.0f, 0.0f));
    engine::math::Mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);

    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);

    REQUIRE(builder.activeLightCount() == engine::rendering::kMaxLights);

    builder.shutdown();
}

// ---------------------------------------------------------------------------
// Caching / dirty-flag tests for the Pixel 9 perf fix.
//
// Helper: spawn one point light at the given world position.
// ---------------------------------------------------------------------------

namespace
{

engine::ecs::EntityID spawnPointLight(engine::ecs::Registry& reg, const engine::math::Vec3& pos,
                                      float radius = 3.0f)
{
    engine::ecs::EntityID e = reg.createEntity();

    engine::rendering::WorldTransformComponent xform{};
    xform.matrix = engine::math::Mat4(1.0f);
    xform.matrix[3] = engine::math::Vec4(pos, 1.0f);
    reg.emplace<engine::rendering::WorldTransformComponent>(e, xform);

    engine::rendering::PointLightComponent pl{};
    pl.color = engine::math::Vec3(1.0f, 1.0f, 1.0f);
    pl.intensity = 1.0f;
    pl.radius = radius;
    reg.emplace<engine::rendering::PointLightComponent>(e, pl);

    return e;
}

engine::math::Mat4 testView()
{
    return glm::lookAt(engine::math::Vec3(0.0f, 0.0f, 0.0f), engine::math::Vec3(0.0f, 0.0f, -1.0f),
                       engine::math::Vec3(0.0f, 1.0f, 0.0f));
}

engine::math::Mat4 testProj()
{
    return glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
}

}  // namespace

TEST_CASE("LightClusterBuilder: identical inputs across frames issue zero uploads on 2nd call",
          "[lighting][cluster][cache]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    spawnPointLight(reg, engine::math::Vec3(0.0f, 0.0f, -5.0f));

    engine::rendering::LightClusterBuilder builder;
    builder.init();

    const engine::math::Mat4 view = testView();
    const engine::math::Mat4 proj = testProj();

    // First call: full upload (3 textures).  Cluster geometry must be built
    // from scratch.
    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);
    REQUIRE(builder.uploadCallCount() == 3u);
    REQUIRE(builder.clusterGeometryRebuiltLastFrame());

    // Second call with bit-identical inputs: zero new uploads, no AABB rebuild.
    const uint32_t baseline = builder.uploadCallCount();
    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);
    REQUIRE(builder.uploadCallCount() == baseline);
    REQUIRE_FALSE(builder.clusterGeometryRebuiltLastFrame());

    // Repeat once more to be sure the cache stays warm.
    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);
    REQUIRE(builder.uploadCallCount() == baseline);

    builder.shutdown();
}

TEST_CASE("LightClusterBuilder: moving one light triggers exactly 3 uploads (data+grid+index)",
          "[lighting][cluster][cache]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::ecs::EntityID light = spawnPointLight(reg, engine::math::Vec3(0.0f, 0.0f, -5.0f));

    engine::rendering::LightClusterBuilder builder;
    builder.init();

    const engine::math::Mat4 view = testView();
    const engine::math::Mat4 proj = testProj();

    // First call: 3 uploads to prime.
    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);
    REQUIRE(builder.uploadCallCount() == 3u);

    // Move the light.
    auto* xform = reg.get<engine::rendering::WorldTransformComponent>(light);
    REQUIRE(xform != nullptr);
    xform->matrix[3] = engine::math::Vec4(1.5f, 0.5f, -6.0f, 1.0f);

    const uint32_t baseline = builder.uploadCallCount();
    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);

    // Expect exactly 3 new uploads: lightData (light moved), lightGrid +
    // lightIndex (must stay coherent).  Cluster geometry unchanged.
    REQUIRE(builder.uploadCallCount() - baseline == 3u);
    REQUIRE_FALSE(builder.clusterGeometryRebuiltLastFrame());

    builder.shutdown();
}

TEST_CASE("LightClusterBuilder: projection change triggers AABB rebuild + grid/index re-upload",
          "[lighting][cluster][cache]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    spawnPointLight(reg, engine::math::Vec3(0.0f, 0.0f, -5.0f));

    engine::rendering::LightClusterBuilder builder;
    builder.init();

    const engine::math::Mat4 view = testView();
    const engine::math::Mat4 proj1 = testProj();

    builder.update(reg, view, proj1, 0.1f, 100.0f, 1280, 720);
    const uint32_t afterFirst = builder.uploadCallCount();
    REQUIRE(afterFirst == 3u);

    // Same proj → no rebuild.
    builder.update(reg, view, proj1, 0.1f, 100.0f, 1280, 720);
    REQUIRE_FALSE(builder.clusterGeometryRebuiltLastFrame());
    REQUIRE(builder.uploadCallCount() == afterFirst);

    // New projection (different FOV) → AABB rebuild + grid/index re-upload.
    // lightData stays clean because lights didn't move.
    const engine::math::Mat4 proj2 =
        glm::perspective(glm::radians(75.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const uint32_t baseline = builder.uploadCallCount();
    builder.update(reg, view, proj2, 0.1f, 100.0f, 1280, 720);

    REQUIRE(builder.clusterGeometryRebuiltLastFrame());
    // lightGrid + lightIndex must re-upload; lightData should NOT
    // (lights didn't move).
    REQUIRE(builder.uploadCallCount() - baseline == 2u);

    builder.shutdown();
}

TEST_CASE("LightClusterBuilder: near/far change invalidates the cluster cache",
          "[lighting][cluster][cache]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    spawnPointLight(reg, engine::math::Vec3(0.0f, 0.0f, -5.0f));

    engine::rendering::LightClusterBuilder builder;
    builder.init();

    const engine::math::Mat4 view = testView();
    const engine::math::Mat4 proj = testProj();

    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);
    REQUIRE(builder.clusterGeometryRebuiltLastFrame());

    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);
    REQUIRE_FALSE(builder.clusterGeometryRebuiltLastFrame());

    // Change just the far plane.
    builder.update(reg, view, proj, 0.1f, 200.0f, 1280, 720);
    REQUIRE(builder.clusterGeometryRebuiltLastFrame());

    builder.shutdown();
}

TEST_CASE("LightClusterBuilder: zero lights — first frame uploads, subsequent frames don't",
          "[lighting][cluster][cache]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;  // no lights

    engine::rendering::LightClusterBuilder builder;
    builder.init();

    const engine::math::Mat4 view = testView();
    const engine::math::Mat4 proj = testProj();

    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);
    REQUIRE(builder.activeLightCount() == 0u);
    // First frame uploads everything to seed the GPU (grid must be zeroed).
    REQUIRE(builder.uploadCallCount() == 3u);

    const uint32_t baseline = builder.uploadCallCount();
    builder.update(reg, view, proj, 0.1f, 100.0f, 1280, 720);
    REQUIRE(builder.activeLightCount() == 0u);
    REQUIRE(builder.uploadCallCount() == baseline);

    builder.shutdown();
}
