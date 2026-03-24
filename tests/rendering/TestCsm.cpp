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
