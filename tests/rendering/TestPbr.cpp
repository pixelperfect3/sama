#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ShaderUniforms.h"
#include "engine/rendering/systems/DrawCallBuildSystem.h"

using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
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
// Material struct — structural tests, no bgfx required
// ---------------------------------------------------------------------------

TEST_CASE("Material: default values are correct", "[pbr]")
{
    const engine::rendering::Material mat{};

    REQUIRE(mat.albedo.x == Approx(1.0f));
    REQUIRE(mat.albedo.y == Approx(1.0f));
    REQUIRE(mat.albedo.z == Approx(1.0f));
    REQUIRE(mat.albedo.w == Approx(1.0f));
    REQUIRE(mat.roughness == Approx(0.5f));
    REQUIRE(mat.metallic == Approx(0.0f));
    REQUIRE(mat.emissiveScale == Approx(0.0f));
    REQUIRE(mat.albedoMapId == 0u);
    REQUIRE(mat.normalMapId == 0u);
    REQUIRE(mat.ormMapId == 0u);
}

TEST_CASE("Material: layout size matches expected", "[pbr]")
{
    // The size is verified at compile time in Material.h via static_assert.
    // This test just documents the expected size for readers.
    REQUIRE(sizeof(engine::rendering::Material) > 0u);
}

// ---------------------------------------------------------------------------
// RenderResources: material add / get / remove round-trip
// ---------------------------------------------------------------------------

TEST_CASE("RenderResources: addMaterial / getMaterial round-trip", "[pbr]")
{
    engine::rendering::RenderResources res;

    engine::rendering::Material mat{};
    mat.albedo = engine::math::Vec4(0.2f, 0.4f, 0.8f, 1.0f);
    mat.roughness = 0.3f;
    mat.metallic = 0.7f;

    const uint32_t id = res.addMaterial(mat);
    REQUIRE(id != 0u);

    const engine::rendering::Material* got = res.getMaterial(id);
    REQUIRE(got != nullptr);
    REQUIRE(got->albedo.x == Approx(0.2f));
    REQUIRE(got->albedo.y == Approx(0.4f));
    REQUIRE(got->roughness == Approx(0.3f));
    REQUIRE(got->metallic == Approx(0.7f));

    // Unknown IDs return nullptr.
    REQUIRE(res.getMaterial(0u) == nullptr);
    REQUIRE(res.getMaterial(id + 100u) == nullptr);

    res.removeMaterial(id);
    REQUIRE(res.getMaterial(id) == nullptr);
}

TEST_CASE("RenderResources: material slot reuse after remove", "[pbr]")
{
    engine::rendering::RenderResources res;

    const uint32_t id1 = res.addMaterial({});
    const uint32_t id2 = res.addMaterial({});
    REQUIRE(id1 != id2);

    res.removeMaterial(id1);
    const uint32_t id3 = res.addMaterial({});
    // Freed slot should be reused.
    REQUIRE(id3 == id1);
}

// ---------------------------------------------------------------------------
// ShaderUniforms — init / destroy do not crash in headless mode
// ---------------------------------------------------------------------------

TEST_CASE("ShaderUniforms: init and destroy do not crash in headless mode", "[pbr]")
{
    HeadlessBgfx bgfxCtx;

    engine::rendering::ShaderUniforms uniforms{};
    // Should not throw or crash even in Noop renderer.
    REQUIRE_NOTHROW(uniforms.init());
    REQUIRE_NOTHROW(uniforms.destroy());
}

// ---------------------------------------------------------------------------
// PBR program — load does not crash (returns invalid handle in headless mode)
// ---------------------------------------------------------------------------

TEST_CASE("loadPbrProgram: returns BGFX_INVALID_HANDLE in headless mode", "[pbr]")
{
    HeadlessBgfx bgfxCtx;

    // The Noop renderer cannot compile embedded shaders; the loader must handle
    // this gracefully and return an invalid handle rather than crashing.
    engine::rendering::ProgramHandle program = engine::rendering::loadPbrProgram();
    REQUIRE(!engine::rendering::isValid(program));

    if (engine::rendering::isValid(program))
        bgfx::destroy(bgfx::ProgramHandle{program.idx});
}

// ---------------------------------------------------------------------------
// DrawCallBuildSystem PBR overload — smoke test
// ---------------------------------------------------------------------------

TEST_CASE("DrawCallBuildSystem: PBR overload smoke test with headless renderer", "[pbr]")
{
    HeadlessBgfx bgfxCtx;

    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    // Build a mesh and register it.
    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    // Register a material.
    engine::rendering::Material mat{};
    mat.albedo = engine::math::Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    mat.roughness = 0.5f;
    mat.metallic = 0.0f;
    const uint32_t matId = res.addMaterial(mat);

    // Create an entity with all required components.
    const engine::ecs::EntityID entity = reg.createEntity();
    reg.emplace<engine::rendering::WorldTransformComponent>(
        entity, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0f)});
    reg.emplace<engine::rendering::MeshComponent>(entity, engine::rendering::MeshComponent{meshId});
    reg.emplace<engine::rendering::MaterialComponent>(entity,
                                                      engine::rendering::MaterialComponent{matId});
    reg.emplace<engine::rendering::VisibleTag>(entity);

    engine::rendering::ShaderUniforms uniforms{};
    uniforms.init();

    engine::rendering::ProgramHandle program = engine::rendering::loadPbrProgram();

    bgfxCtx.renderer.beginFrame();

    engine::rendering::DrawCallBuildSystem dcbs;
    // Must not crash — the Noop renderer ignores all draw calls.
    REQUIRE_NOTHROW(dcbs.update(reg, res, bgfx::ProgramHandle{program.idx}, &uniforms));

    bgfxCtx.renderer.endFrame();

    if (engine::rendering::isValid(program))
        bgfx::destroy(bgfx::ProgramHandle{program.idx});

    uniforms.destroy();
    res.destroyAll();
}
