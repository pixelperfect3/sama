#include <ankerl/unordered_dense.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <memory_resource>
#include <vector>

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

// ---------------------------------------------------------------------------
// Microbenchmark — audit item line 143.
//
// Measures the cost of constructing + populating the grouping map for one
// frame, comparing the old default-heap shape against the new pmr-arena
// shape.  Tagged [!benchmark] so it doesn't run by default; trigger via:
//
//   build/engine_tests "[instancing-bench]"
//
// We don't drive a full InstanceBufferBuildSystem::update() call here —
// the audit's claim is specifically about the map allocation, and the
// rest of the system (cull check, encoder submit, etc.) is workload-
// dependent.  Isolating the map lets us A/B that one thing cleanly.
// ---------------------------------------------------------------------------

namespace
{

struct BenchGroupData
{
    uint32_t meshId = 0;
    std::pmr::vector<engine::math::Mat4> instances;
    bool anyVisible = false;

    explicit BenchGroupData(std::pmr::memory_resource* mr) : instances(mr) {}
};

template <typename T>
inline void doNotOptimizeRef(T& value)
{
    asm volatile("" : "+r,m"(value) : : "memory");
}

}  // namespace

TEST_CASE("BENCH: instancing groups map default-heap vs pmr-arena",
          "[instancing-bench][!benchmark]")
{
    constexpr int kIterations = 10000;
    constexpr int kGroups = 8;
    constexpr int kInstancesPerGroup = 32;

    const engine::math::Mat4 sampleMatrix(1.0f);

    using Clock = std::chrono::steady_clock;

    // -- Default heap (the old shape) --------------------------------------
    const auto refStart = Clock::now();
    for (int iter = 0; iter < kIterations; ++iter)
    {
        ankerl::unordered_dense::map<uint32_t, BenchGroupData> groups;
        for (int g = 0; g < kGroups; ++g)
        {
            auto [it, inserted] =
                groups.try_emplace(static_cast<uint32_t>(g), std::pmr::get_default_resource());
            auto& gd = it->second;
            if (inserted)
            {
                gd.meshId = static_cast<uint32_t>(g);
            }
            for (int i = 0; i < kInstancesPerGroup; ++i)
            {
                gd.instances.push_back(sampleMatrix);
                gd.anyVisible = true;
            }
        }
        doNotOptimizeRef(groups);
    }
    const auto refNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - refStart)
                           .count();

    // -- pmr arena (the new shape) -----------------------------------------
    // Pre-allocate a 256 KB buffer for the monotonic resource — comfortably
    // covers the per-frame allocations.  We release() between iterations
    // to simulate the FrameArena::reset() that the engine performs at
    // end-of-frame.
    std::array<std::byte, 256 * 1024> arenaBuffer{};
    std::pmr::monotonic_buffer_resource arena(arenaBuffer.data(), arenaBuffer.size());

    const auto fastStart = Clock::now();
    for (int iter = 0; iter < kIterations; ++iter)
    {
        arena.release();
        std::pmr::polymorphic_allocator<std::pair<uint32_t, BenchGroupData>> mapAlloc(&arena);
        ankerl::unordered_dense::pmr::map<uint32_t, BenchGroupData> groups(mapAlloc);
        groups.reserve(16);
        for (int g = 0; g < kGroups; ++g)
        {
            auto [it, inserted] = groups.try_emplace(static_cast<uint32_t>(g), &arena);
            auto& gd = it->second;
            if (inserted)
            {
                gd.meshId = static_cast<uint32_t>(g);
            }
            for (int i = 0; i < kInstancesPerGroup; ++i)
            {
                gd.instances.push_back(sampleMatrix);
                gd.anyVisible = true;
            }
        }
        doNotOptimizeRef(groups);
    }
    const auto fastNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - fastStart).count();

    const double refNsPerIter = static_cast<double>(refNs) / static_cast<double>(kIterations);
    const double fastNsPerIter = static_cast<double>(fastNs) / static_cast<double>(kIterations);
    const double speedup = refNsPerIter / fastNsPerIter;

    std::printf("\n[BENCH instancing-groups] %d iters, %d groups x %d instances: "
                "%.0f ns/frame (default-heap) vs %.0f ns/frame (pmr) — %.2fx speedup\n",
                kIterations, kGroups, kInstancesPerGroup, refNsPerIter, fastNsPerIter, speedup);

    CHECK(fastNsPerIter < refNsPerIter);  // Sanity: pmr IS faster.
}
