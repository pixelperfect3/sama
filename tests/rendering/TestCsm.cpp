#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/ecs/Registry.h"
#include "engine/math/Frustum.h"
#include "engine/math/Types.h"
#include "engine/rendering/CsmSplitCalculator.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShadowRenderer.h"
#include "engine/rendering/systems/ShadowCullSystem.h"

using Catch::Approx;

// ---------------------------------------------------------------------------
// Headless bgfx fixture (Noop renderer)
// ---------------------------------------------------------------------------

struct CsmHeadlessBgfx
{
    engine::rendering::Renderer renderer;

    CsmHeadlessBgfx()
    {
        engine::rendering::RendererDesc desc{};
        desc.headless = true;
        desc.width = 1;
        desc.height = 1;
        REQUIRE(renderer.init(desc));
    }

    ~CsmHeadlessBgfx()
    {
        renderer.shutdown();
    }

    CsmHeadlessBgfx(const CsmHeadlessBgfx&) = delete;
    CsmHeadlessBgfx& operator=(const CsmHeadlessBgfx&) = delete;
};

// ---------------------------------------------------------------------------
// computeCsmSplits — 3 cascades cover [near, far] range
// ---------------------------------------------------------------------------

TEST_CASE("CsmSplits: 3 cascades cover [near, far] range", "[csm]")
{
    const float near = 0.1f;
    const float far = 100.0f;
    const float lambda = 0.5f;
    const uint32_t count = 3;

    engine::rendering::CsmSplits splits =
        engine::rendering::computeCsmSplits(count, near, far, lambda);

    REQUIRE(splits.count == count);
    REQUIRE(splits.nearPlane == Approx(near));

    // The last split distance must equal (or be very close to) the far plane.
    REQUIRE(splits.splitDistances[count - 1] == Approx(far).margin(1e-3f));
}

// ---------------------------------------------------------------------------
// computeCsmSplits — splits are strictly increasing
// ---------------------------------------------------------------------------

TEST_CASE("CsmSplits: splits are strictly increasing", "[csm]")
{
    const float near = 0.1f;
    const float far = 200.0f;
    const float lambda = 0.5f;
    const uint32_t count = 3;

    engine::rendering::CsmSplits splits =
        engine::rendering::computeCsmSplits(count, near, far, lambda);

    // Each successive split distance must be larger than the previous.
    for (uint32_t i = 1; i < count; ++i)
    {
        REQUIRE(splits.splitDistances[i] > splits.splitDistances[i - 1]);
    }

    // First split must be greater than the near plane.
    REQUIRE(splits.splitDistances[0] > near);
}

// ---------------------------------------------------------------------------
// computeCsmSplits — lambda=0 gives linear splits
// ---------------------------------------------------------------------------

TEST_CASE("CsmSplits: lambda=0 gives linear splits", "[csm]")
{
    const float near = 1.0f;
    const float far = 100.0f;
    const uint32_t count = 3;

    engine::rendering::CsmSplits splits =
        engine::rendering::computeCsmSplits(count, near, far, 0.0f);

    // With lambda=0 the formula reduces to: cUni_i = near + (far - near) * (i / N)
    for (uint32_t i = 1; i <= count; ++i)
    {
        float expected = near + (far - near) * (static_cast<float>(i) / static_cast<float>(count));
        REQUIRE(splits.splitDistances[i - 1] == Approx(expected).margin(1e-4f));
    }
}

// ---------------------------------------------------------------------------
// computeCsmSplits — lambda=1 gives log splits
// ---------------------------------------------------------------------------

TEST_CASE("CsmSplits: lambda=1 gives log splits", "[csm]")
{
    const float near = 1.0f;
    const float far = 100.0f;
    const uint32_t count = 3;

    engine::rendering::CsmSplits splits =
        engine::rendering::computeCsmSplits(count, near, far, 1.0f);

    // With lambda=1 the formula reduces to: cLog_i = near * (far/near)^(i/N)
    for (uint32_t i = 1; i <= count; ++i)
    {
        float p = static_cast<float>(i) / static_cast<float>(count);
        float expected = near * std::pow(far / near, p);
        REQUIRE(splits.splitDistances[i - 1] == Approx(expected).margin(1e-4f));
    }
}

// ---------------------------------------------------------------------------
// cascadeLightProj — returns a mat4 without crashing
// ---------------------------------------------------------------------------

TEST_CASE("cascadeLightProj: returns mat4 without crashing", "[csm]")
{
    const engine::math::Mat4 cameraView =
        glm::lookAt(engine::math::Vec3(0.0f, 5.0f, 10.0f), engine::math::Vec3(0.0f, 0.0f, 0.0f),
                    engine::math::Vec3(0.0f, 1.0f, 0.0f));

    const engine::math::Mat4 lightView =
        glm::lookAt(engine::math::Vec3(50.0f, 100.0f, 50.0f), engine::math::Vec3(0.0f, 0.0f, 0.0f),
                    engine::math::Vec3(0.0f, 1.0f, 0.0f));

    const float fovY = glm::radians(60.0f);
    const float aspectRatio = 16.0f / 9.0f;
    const float nearSplit = 0.1f;
    const float farSplit = 10.0f;

    engine::math::Mat4 proj = engine::rendering::cascadeLightProj(cameraView, lightView, fovY,
                                                                  aspectRatio, nearSplit, farSplit);

    // The result must be a non-identity, finite matrix (no NaN / Inf).
    bool hasFiniteValues = true;
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(proj[col][row]))
                hasFiniteValues = false;
        }
    }
    REQUIRE(hasFiniteValues);
}

// ---------------------------------------------------------------------------
// ShadowRenderer — 3-cascade init does not crash in headless mode
// ---------------------------------------------------------------------------

TEST_CASE("ShadowRenderer: 3-cascade init does not crash in headless mode", "[csm]")
{
    CsmHeadlessBgfx bgfxCtx;

    engine::rendering::ShadowRenderer shadowRenderer;
    engine::rendering::ShadowDesc desc{};
    desc.resolution = 2048;
    desc.cascadeCount = 3;

    REQUIRE_NOTHROW(shadowRenderer.init(desc));
    REQUIRE_NOTHROW(shadowRenderer.shutdown());
}

// ---------------------------------------------------------------------------
// ShadowCullSystem — entity in frustum gets correct cascadeMask for 3 cascades
// ---------------------------------------------------------------------------

TEST_CASE("ShadowCullSystem: entity in frustum gets correct cascadeMask for 3 cascades", "[csm]")
{
    CsmHeadlessBgfx bgfxCtx;
    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    // Build a unit cube centred at the origin.
    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    const engine::ecs::EntityID entity = reg.createEntity();

    engine::rendering::WorldTransformComponent wtc{engine::math::Mat4(1.0f)};
    reg.emplace<engine::rendering::WorldTransformComponent>(entity, wtc);
    reg.emplace<engine::rendering::MeshComponent>(entity, engine::rendering::MeshComponent{meshId});

    // Build three frustums that all enclose the origin, one per cascade.
    const engine::math::Mat4 lightView =
        glm::lookAt(engine::math::Vec3(0.0f, 10.0f, 0.0f), engine::math::Vec3(0.0f, 0.0f, 0.0f),
                    engine::math::Vec3(0.0f, 0.0f, -1.0f));
    const engine::math::Mat4 lightProj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 20.0f);
    const engine::math::Frustum shadowFrustum(lightProj * lightView);

    engine::rendering::ShadowCullSystem scs;

    // Run the cull system for all 3 cascades using the same frustum.
    scs.update(reg, res, shadowFrustum, 0);
    scs.update(reg, res, shadowFrustum, 1);
    scs.update(reg, res, shadowFrustum, 2);

    // The entity should be visible in all three cascades (bits 0, 1, and 2 set).
    REQUIRE(reg.has<engine::rendering::ShadowVisibleTag>(entity));

    const uint8_t mask = reg.get<engine::rendering::ShadowVisibleTag>(entity)->cascadeMask;
    REQUIRE((mask & 0x01) != 0);  // cascade 0
    REQUIRE((mask & 0x02) != 0);  // cascade 1
    REQUIRE((mask & 0x04) != 0);  // cascade 2

    res.destroyAll();
}

// ---------------------------------------------------------------------------
// ShadowCullSystem multi-cascade overload — matches the per-cascade result
//
// Audit item line 43.  The new `update(reg, res, span<Frustum>, base)`
// overload walks the mesh view once and tests all N frustums per entity.
// It must produce a byte-identical `cascadeMask` to N calls of the
// per-cascade overload — that's the safety contract for the optimisation.
// ---------------------------------------------------------------------------

#include <array>
#include <span>

TEST_CASE("ShadowCullSystem multi-cascade overload matches N per-cascade calls", "[csm][shadow]")
{
    CsmHeadlessBgfx bgfxCtx;
    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    // Set up two entities — one inside all cascades, one outside all of
    // them — to exercise both branches.
    const engine::ecs::EntityID insideEntity = reg.createEntity();
    reg.emplace<engine::rendering::WorldTransformComponent>(
        insideEntity, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0F)});
    reg.emplace<engine::rendering::MeshComponent>(insideEntity,
                                                  engine::rendering::MeshComponent{meshId});

    const engine::ecs::EntityID outsideEntity = reg.createEntity();
    engine::math::Mat4 farAwayMatrix(1.0F);
    farAwayMatrix[3] = engine::math::Vec4(1000.0F, 1000.0F, 1000.0F, 1.0F);
    reg.emplace<engine::rendering::WorldTransformComponent>(
        outsideEntity, engine::rendering::WorldTransformComponent{farAwayMatrix});
    reg.emplace<engine::rendering::MeshComponent>(outsideEntity,
                                                  engine::rendering::MeshComponent{meshId});

    // Three cascade frustums (same as the test above — all enclose the
    // origin, none enclose (1000, 1000, 1000)).
    const engine::math::Mat4 lightView =
        glm::lookAt(engine::math::Vec3(0.0F, 10.0F, 0.0F), engine::math::Vec3(0.0F, 0.0F, 0.0F),
                    engine::math::Vec3(0.0F, 0.0F, -1.0F));
    const engine::math::Mat4 lightProj = glm::ortho(-10.0F, 10.0F, -10.0F, 10.0F, 0.1F, 20.0F);
    const engine::math::Frustum cascade0(lightProj * lightView);
    const engine::math::Frustum cascade1(lightProj * lightView);
    const engine::math::Frustum cascade2(lightProj * lightView);

    // Reference path: three per-cascade calls.
    engine::rendering::ShadowCullSystem scsRef;
    scsRef.update(reg, res, cascade0, 0);
    scsRef.update(reg, res, cascade1, 1);
    scsRef.update(reg, res, cascade2, 2);
    const uint8_t refInsideMask =
        reg.has<engine::rendering::ShadowVisibleTag>(insideEntity)
            ? reg.get<engine::rendering::ShadowVisibleTag>(insideEntity)->cascadeMask
            : uint8_t{0};
    const bool refOutsideHasTag = reg.has<engine::rendering::ShadowVisibleTag>(outsideEntity);

    // Reset for the multi-cascade path.  Strip the tags so the new call
    // starts from the same blank slate.
    if (reg.has<engine::rendering::ShadowVisibleTag>(insideEntity))
    {
        reg.remove<engine::rendering::ShadowVisibleTag>(insideEntity);
    }
    if (reg.has<engine::rendering::ShadowVisibleTag>(outsideEntity))
    {
        reg.remove<engine::rendering::ShadowVisibleTag>(outsideEntity);
    }

    // Optimised path: single multi-cascade call.
    const std::array<engine::math::Frustum, 3> cascades{cascade0, cascade1, cascade2};
    engine::rendering::ShadowCullSystem scsOpt;
    scsOpt.update(reg, res, std::span<const engine::math::Frustum>{cascades});
    const uint8_t optInsideMask =
        reg.has<engine::rendering::ShadowVisibleTag>(insideEntity)
            ? reg.get<engine::rendering::ShadowVisibleTag>(insideEntity)->cascadeMask
            : uint8_t{0};
    const bool optOutsideHasTag = reg.has<engine::rendering::ShadowVisibleTag>(outsideEntity);

    // Byte-identical results.
    REQUIRE(optInsideMask == refInsideMask);
    REQUIRE(optInsideMask == 0x07);  // bits 0, 1, 2 set
    REQUIRE(optOutsideHasTag == refOutsideHasTag);
    REQUIRE(optOutsideHasTag == false);

    res.destroyAll();
}

TEST_CASE("ShadowCullSystem multi-cascade preserves bits outside its range", "[csm][shadow]")
{
    // The contract: the multi-cascade overload owns the bits in
    // [baseCascadeIdx, baseCascadeIdx + N).  Bits outside that range
    // survive.  This lets a caller drive cascades 0..2 in bulk while a
    // separate caller manages bit 3 for, say, a spot-light shadow.
    CsmHeadlessBgfx bgfxCtx;
    engine::ecs::Registry reg;
    engine::rendering::RenderResources res;

    const uint32_t meshId =
        res.addMesh(engine::rendering::buildMesh(engine::rendering::makeCubeMeshData()));

    const engine::ecs::EntityID entity = reg.createEntity();
    reg.emplace<engine::rendering::WorldTransformComponent>(
        entity, engine::rendering::WorldTransformComponent{engine::math::Mat4(1.0F)});
    reg.emplace<engine::rendering::MeshComponent>(entity, engine::rendering::MeshComponent{meshId});

    // Pre-populate bit 4 (an external owner — e.g. a spot light).
    engine::rendering::ShadowVisibleTag tag{};
    tag.cascadeMask = 0x10;  // bit 4
    reg.emplace<engine::rendering::ShadowVisibleTag>(entity, tag);

    const engine::math::Mat4 lightView =
        glm::lookAt(engine::math::Vec3(0.0F, 10.0F, 0.0F), engine::math::Vec3(0.0F, 0.0F, 0.0F),
                    engine::math::Vec3(0.0F, 0.0F, -1.0F));
    const engine::math::Mat4 lightProj = glm::ortho(-10.0F, 10.0F, -10.0F, 10.0F, 0.1F, 20.0F);
    const engine::math::Frustum cascade0(lightProj * lightView);
    const engine::math::Frustum cascade1(lightProj * lightView);

    // Cull cascades 0..1 only.  Bit 4 must survive.
    const std::array<engine::math::Frustum, 2> cascades{cascade0, cascade1};
    engine::rendering::ShadowCullSystem scs;
    scs.update(reg, res, std::span<const engine::math::Frustum>{cascades}, /*baseCascadeIdx=*/0);

    const uint8_t mask = reg.get<engine::rendering::ShadowVisibleTag>(entity)->cascadeMask;
    REQUIRE((mask & 0x01) != 0);  // cascade 0 set (entity inside)
    REQUIRE((mask & 0x02) != 0);  // cascade 1 set (entity inside)
    REQUIRE((mask & 0x10) != 0);  // bit 4 preserved
    REQUIRE((mask & 0xE0) == 0);  // bits 5..7 still zero
    REQUIRE((mask & 0x0C) == 0);  // bits 2, 3 still zero (not in our range)

    res.destroyAll();
}
