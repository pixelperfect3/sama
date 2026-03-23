#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/ecs/Registry.h"
#include "engine/math/Frustum.h"
#include "engine/math/Transform.h"
#include "engine/math/Types.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"
#include "engine/rendering/systems/FrustumCullSystem.h"

using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// RAII wrapper that inits bgfx in headless mode and shuts it down on destruction.
// Each test case that needs bgfx creates one of these as a local variable.
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

    // Non-copyable, non-movable.
    HeadlessBgfx(const HeadlessBgfx&) = delete;
    HeadlessBgfx& operator=(const HeadlessBgfx&) = delete;
};

// ---------------------------------------------------------------------------
// MeshData structural tests — no bgfx required
// ---------------------------------------------------------------------------

TEST_CASE("makeCubeMeshData: basic structural validity", "[mesh]")
{
    const engine::rendering::MeshData data = engine::rendering::makeCubeMeshData();

    // 6 faces × 4 corners = 24 vertices → 72 floats
    REQUIRE(data.positions.size() == 24 * 3);

    // Surface attributes must be non-empty and parallel.
    REQUIRE(data.normals.size() == 24 * 2);
    REQUIRE(data.tangents.size() == 24 * 4);
    REQUIRE(data.uvs.size() == 24 * 2);

    // 6 faces × 2 triangles × 3 indices = 36 indices
    REQUIRE(data.indices.size() == 36);

    // Bounds should be ±0.5.
    REQUIRE(data.boundsMin.x == Approx(-0.5f));
    REQUIRE(data.boundsMin.y == Approx(-0.5f));
    REQUIRE(data.boundsMin.z == Approx(-0.5f));
    REQUIRE(data.boundsMax.x == Approx(0.5f));
    REQUIRE(data.boundsMax.y == Approx(0.5f));
    REQUIRE(data.boundsMax.z == Approx(0.5f));

    // All indices must be in range [0, 23].
    for (uint16_t idx : data.indices)
        REQUIRE(idx < 24);
}

// ---------------------------------------------------------------------------
// buildMesh — bgfx required
// ---------------------------------------------------------------------------

TEST_CASE("buildMesh: valid cube mesh with surface stream", "[mesh]")
{
    HeadlessBgfx bgfxCtx;

    const engine::rendering::MeshData data = engine::rendering::makeCubeMeshData();
    engine::rendering::Mesh mesh = engine::rendering::buildMesh(data);

    REQUIRE(mesh.isValid());
    REQUIRE(mesh.vertexCount == 24);
    REQUIRE(mesh.indexCount == 36);
    REQUIRE(bgfx::isValid(mesh.positionVbh));
    REQUIRE(bgfx::isValid(mesh.surfaceVbh));
    REQUIRE(bgfx::isValid(mesh.ibh));

    mesh.destroy();
    REQUIRE(!mesh.isValid());
}

TEST_CASE("buildMesh: empty positions returns invalid mesh", "[mesh]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::MeshData data;
    // positions empty — should fail gracefully.
    data.indices = {0, 1, 2};
    const engine::rendering::Mesh mesh = engine::rendering::buildMesh(data);
    REQUIRE(!mesh.isValid());
}

TEST_CASE("buildMesh: position-only mesh (no surface stream)", "[mesh]")
{
    HeadlessBgfx bgfxCtx;

    // Three vertices forming a single triangle; no surface attributes.
    engine::rendering::MeshData data;
    data.positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    data.indices = {0, 1, 2};
    data.boundsMin = engine::math::Vec3(0.0f, 0.0f, 0.0f);
    data.boundsMax = engine::math::Vec3(1.0f, 1.0f, 0.0f);

    engine::rendering::Mesh mesh = engine::rendering::buildMesh(data);
    REQUIRE(mesh.isValid());
    REQUIRE(!bgfx::isValid(mesh.surfaceVbh));  // no surface stream

    mesh.destroy();
}

// ---------------------------------------------------------------------------
// RenderResources — add / get / remove round-trip
// ---------------------------------------------------------------------------

TEST_CASE("RenderResources: addMesh / getMesh round-trip", "[mesh]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::RenderResources res;

    const engine::rendering::MeshData data = engine::rendering::makeCubeMeshData();
    engine::rendering::Mesh mesh = engine::rendering::buildMesh(data);
    REQUIRE(mesh.isValid());

    const uint32_t id = res.addMesh(std::move(mesh));
    REQUIRE(id != 0);

    const engine::rendering::Mesh* got = res.getMesh(id);
    REQUIRE(got != nullptr);
    REQUIRE(got->isValid());
    REQUIRE(got->vertexCount == 24);

    // Unknown IDs return nullptr.
    REQUIRE(res.getMesh(0) == nullptr);
    REQUIRE(res.getMesh(id + 100) == nullptr);

    res.removeMesh(id);
    REQUIRE(res.getMesh(id) == nullptr);

    res.destroyAll();
}

TEST_CASE("RenderResources: slot reuse after remove", "[mesh]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::RenderResources res;

    auto makeMesh = []() -> engine::rendering::Mesh
    {
        const engine::rendering::MeshData data = engine::rendering::makeCubeMeshData();
        return engine::rendering::buildMesh(data);
    };

    const uint32_t id1 = res.addMesh(makeMesh());
    const uint32_t id2 = res.addMesh(makeMesh());
    REQUIRE(id1 != id2);

    res.removeMesh(id1);
    const uint32_t id3 = res.addMesh(makeMesh());
    // Slot 1 was freed and should be reused.
    REQUIRE(id3 == id1);

    res.destroyAll();
}

// ---------------------------------------------------------------------------
// FrustumCullSystem
// ---------------------------------------------------------------------------

TEST_CASE("FrustumCullSystem: entity inside frustum receives VisibleTag", "[frustum_cull]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    // Build a unit cube mesh and register it.
    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    // Place entity at origin, well inside a standard perspective frustum.
    const engine::ecs::EntityID entity = reg.createEntity();
    reg.emplace<engine::rendering::WorldTransformComponent>(
        entity, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0f)});
    reg.emplace<engine::rendering::MeshComponent>(entity, engine::rendering::MeshComponent{meshId});

    // Build a frustum from a perspective VP matrix looking down -Z.
    const engine::math::Mat4 view = engine::math::makeLookAt(
        engine::math::Vec3(0, 0, 5), engine::math::Vec3(0, 0, 0), engine::math::Vec3(0, 1, 0));
    const engine::math::Mat4 proj =
        engine::math::makePerspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    const engine::math::Frustum frustum(proj * view);

    engine::rendering::FrustumCullSystem cull;
    cull.update(reg, res, frustum);

    REQUIRE(reg.has<engine::rendering::VisibleTag>(entity));

    res.destroyAll();
}

TEST_CASE("FrustumCullSystem: entity behind camera loses VisibleTag", "[frustum_cull]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    // Place entity far behind the camera (camera looks down -Z from z=5; entity at z=50
    // is behind the near plane and beyond far=20).
    const engine::ecs::EntityID entity = reg.createEntity();

    engine::math::Mat4 worldMat =
        glm::translate(engine::math::Mat4(1.0f), engine::math::Vec3(0.0f, 0.0f, 50.0f));
    reg.emplace<engine::rendering::WorldTransformComponent>(
        entity, engine::rendering::WorldTransformComponent{worldMat});
    reg.emplace<engine::rendering::MeshComponent>(entity, engine::rendering::MeshComponent{meshId});
    // Pre-add VisibleTag to confirm it gets removed.
    reg.emplace<engine::rendering::VisibleTag>(entity);

    const engine::math::Mat4 view = engine::math::makeLookAt(
        engine::math::Vec3(0, 0, 5), engine::math::Vec3(0, 0, 0), engine::math::Vec3(0, 1, 0));
    const engine::math::Mat4 proj =
        engine::math::makePerspective(glm::radians(60.0f), 1.0f, 0.1f, 20.0f);
    const engine::math::Frustum frustum(proj * view);

    engine::rendering::FrustumCullSystem cull;
    cull.update(reg, res, frustum);

    REQUIRE(!reg.has<engine::rendering::VisibleTag>(entity));

    res.destroyAll();
}

// ---------------------------------------------------------------------------
// DrawCallBuildSystem — smoke test (no crash with headless renderer)
// ---------------------------------------------------------------------------

TEST_CASE("DrawCallBuildSystem: smoke test with headless renderer", "[mesh]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    const engine::ecs::EntityID entity = reg.createEntity();
    reg.emplace<engine::rendering::WorldTransformComponent>(
        entity, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0f)});
    reg.emplace<engine::rendering::MeshComponent>(entity, engine::rendering::MeshComponent{meshId});
    reg.emplace<engine::rendering::VisibleTag>(entity);

    // The Noop renderer returns an invalid program handle — DrawCallBuildSystem
    // must handle that gracefully (early-out without crash).
    bgfx::ProgramHandle program = engine::rendering::loadUnlitProgram();

    bgfxCtx.renderer.beginFrame();

    engine::rendering::DrawCallBuildSystem dcbs;
    // Should not crash whether program is valid or not.
    REQUIRE_NOTHROW(dcbs.update(reg, res, program));

    bgfxCtx.renderer.endFrame();

    if (bgfx::isValid(program))
        bgfx::destroy(program);

    res.destroyAll();
}
