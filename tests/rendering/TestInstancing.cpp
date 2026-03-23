#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/Registry.h"
#include "engine/math/Types.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/systems/InstanceBufferBuildSystem.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// RAII bgfx headless init — identical to the pattern in TestMesh.cpp.
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
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("InstanceBufferBuildSystem: no crash when no entities have InstancedMeshComponent",
          "[instancing]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    // An entity with no InstancedMeshComponent should be ignored entirely.
    const engine::ecs::EntityID e = reg.createEntity();
    reg.emplace<engine::rendering::WorldTransformComponent>(
        e, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0f)});

    bgfxCtx.renderer.beginFrame();

    engine::rendering::InstanceBufferBuildSystem sys;
    REQUIRE_NOTHROW(sys.update(reg, res, BGFX_INVALID_HANDLE));

    bgfxCtx.renderer.endFrame();
    res.destroyAll();
}

TEST_CASE(
    "InstanceBufferBuildSystem: 10 entities sharing instanceGroupId=1 do not crash (one group)",
    "[instancing]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    for (int i = 0; i < 10; ++i)
    {
        const engine::ecs::EntityID e = reg.createEntity();
        reg.emplace<engine::rendering::InstancedMeshComponent>(
            e, engine::rendering::InstancedMeshComponent{meshId, /*material=*/0,
                                                         /*instanceGroupId=*/1});
        reg.emplace<engine::rendering::WorldTransformComponent>(
            e, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0f)});
        // Mark all entities visible so the group is not culled.
        reg.emplace<engine::rendering::VisibleTag>(e);
    }

    // The Noop renderer returns BGFX_INVALID_HANDLE for programs, so the system
    // will early-exit. We're primarily verifying no crash / assert fires.
    bgfxCtx.renderer.beginFrame();

    engine::rendering::InstanceBufferBuildSystem sys;
    REQUIRE_NOTHROW(sys.update(reg, res, BGFX_INVALID_HANDLE));

    bgfxCtx.renderer.endFrame();
    res.destroyAll();
}

TEST_CASE("InstanceBufferBuildSystem: group with no visible entities is culled (no crash)",
          "[instancing]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    // Three entities in group 2, none with VisibleTag — whole group should be culled.
    for (int i = 0; i < 3; ++i)
    {
        const engine::ecs::EntityID e = reg.createEntity();
        reg.emplace<engine::rendering::InstancedMeshComponent>(
            e, engine::rendering::InstancedMeshComponent{meshId, /*material=*/0,
                                                         /*instanceGroupId=*/2});
        reg.emplace<engine::rendering::WorldTransformComponent>(
            e, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0f)});
        // Intentionally no VisibleTag.
    }

    bgfxCtx.renderer.beginFrame();

    engine::rendering::InstanceBufferBuildSystem sys;
    REQUIRE_NOTHROW(sys.update(reg, res, BGFX_INVALID_HANDLE));

    bgfxCtx.renderer.endFrame();
    res.destroyAll();
}

TEST_CASE("InstanceBufferBuildSystem: multiple groups are each processed independently (no crash)",
          "[instancing]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    // Group 10: 3 visible entities.
    for (int i = 0; i < 3; ++i)
    {
        const engine::ecs::EntityID e = reg.createEntity();
        reg.emplace<engine::rendering::InstancedMeshComponent>(
            e, engine::rendering::InstancedMeshComponent{meshId, /*material=*/0,
                                                         /*instanceGroupId=*/10});
        reg.emplace<engine::rendering::WorldTransformComponent>(
            e, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0f)});
        reg.emplace<engine::rendering::VisibleTag>(e);
    }

    // Group 11: 5 entities, none visible — should be culled.
    for (int i = 0; i < 5; ++i)
    {
        const engine::ecs::EntityID e = reg.createEntity();
        reg.emplace<engine::rendering::InstancedMeshComponent>(
            e, engine::rendering::InstancedMeshComponent{meshId, /*material=*/0,
                                                         /*instanceGroupId=*/11});
        reg.emplace<engine::rendering::WorldTransformComponent>(
            e, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0f)});
    }

    bgfxCtx.renderer.beginFrame();

    engine::rendering::InstanceBufferBuildSystem sys;
    REQUIRE_NOTHROW(sys.update(reg, res, BGFX_INVALID_HANDLE));

    bgfxCtx.renderer.endFrame();
    res.destroyAll();
}
