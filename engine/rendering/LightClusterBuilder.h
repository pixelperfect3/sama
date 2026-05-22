#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

#include "engine/ecs/Registry.h"
#include "engine/math/Types.h"
#include "engine/rendering/LightData.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// LightClusterBuilder — CPU-side clustered lighting for the Forward+ pipeline.
//
// Responsibilities (called once per frame):
//   1. Collect PointLightComponent / SpotLightComponent entities from the ECS
//      and build a packed LightEntry list (capped at kMaxLights = 256).
//   2. Partition the view frustum into a 16×9×24 cluster grid and test each
//      light sphere against each cluster AABB.
//   3. Upload the results to three RGBA32F textures consumed by fs_pbr.sc:
//        s_lightData  — 256×4:  per-light position/color/direction/angles
//        s_lightGrid  — 3456×1: per-cluster (offset, count) pair
//        s_lightIndex — 8192×1: flat array of light indices
//
// GPU textures are created in init() and destroyed in shutdown().  All CPU
// buffers are fixed-size arrays reused across frames — no per-frame heap
// allocation.
//
// Caching (perf):
//   * Cluster AABBs are a pure function of (projMatrix, nearPlane, farPlane).
//     We fingerprint those inputs and skip the 3,456-entry AABB rebuild when
//     they're unchanged frame-to-frame.
//   * Each of the three texture uploads has its own dirty flag.  lightData is
//     invalidated when the per-light buffer's bytes change; lightGrid and
//     lightIndex are invalidated together (they must stay coherent on the
//     GPU) when either the light buffer or the cluster geometry changed.
//
// Thread safety: single-threaded (runs on the main thread before draw calls).
// ---------------------------------------------------------------------------

class LightClusterBuilder
{
public:
    LightClusterBuilder() = default;
    ~LightClusterBuilder() = default;

    // Non-copyable (owns bgfx handles).
    LightClusterBuilder(const LightClusterBuilder&) = delete;
    LightClusterBuilder& operator=(const LightClusterBuilder&) = delete;

    LightClusterBuilder(LightClusterBuilder&&) = default;
    LightClusterBuilder& operator=(LightClusterBuilder&&) = default;

    // Create the three GPU textures.  Must be called after bgfx::init().
    void init();

    // Destroy GPU textures.  Must be called before bgfx::shutdown().
    void shutdown();

    // Build the cluster grid from the current ECS state and upload to GPU.
    //
    // viewMatrix  — world-to-view transform for the main camera.
    // projMatrix  — view-to-clip (perspective) matrix.
    // nearPlane, farPlane — camera frustum extents (positive distances).
    // screenWidth, screenHeight — framebuffer dimensions in pixels.
    void update(ecs::Registry& reg, const math::Mat4& viewMatrix, const math::Mat4& projMatrix,
                float nearPlane, float farPlane, uint16_t screenWidth, uint16_t screenHeight);

    // Accessors for the three GPU texture handles (valid after init()).
    [[nodiscard]] bgfx::TextureHandle lightDataTex() const
    {
        return lightDataTex_;
    }
    [[nodiscard]] bgfx::TextureHandle lightGridTex() const
    {
        return lightGridTex_;
    }
    [[nodiscard]] bgfx::TextureHandle lightIndexTex() const
    {
        return lightIndexTex_;
    }

    // Number of lights actually uploaded this frame (0 .. kMaxLights).
    [[nodiscard]] uint32_t activeLightCount() const
    {
        return lightCount_;
    }

    // --- Test introspection (cheap, no perf impact) ---

    // Cumulative count of bgfx::updateTexture2D calls issued across all
    // update() invocations on this builder.  Lets tests assert that an
    // idle frame issued zero uploads (and an active frame issued exactly
    // three).
    [[nodiscard]] uint32_t uploadCallCount() const
    {
        return uploadCallCount_;
    }

    // True when the most recent update() rebuilt the 3,456 cluster AABBs
    // (i.e. projection/near/far changed since the previous call).  Latches
    // true on the first call.
    [[nodiscard]] bool clusterGeometryRebuiltLastFrame() const
    {
        return clusterGeometryRebuiltLastFrame_;
    }

private:
    bgfx::TextureHandle lightDataTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle lightGridTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle lightIndexTex_ = BGFX_INVALID_HANDLE;

    uint32_t lightCount_ = 0;

    // Fixed-size CPU buffers — reused every frame, never heap-allocated.
    std::array<LightEntry, kMaxLights> lights_{};
    std::array<ClusterGridEntry, kClusterCount> grid_{};
    std::array<uint32_t, kMaxLightIndices> indices_{};

    // ---------------------------------------------------------------------
    // Cluster AABB cache.  The geometry of every cluster is a pure function
    // of (projMatrix, nearPlane, farPlane); we hoist its computation out of
    // the per-frame fast path so a static camera pays zero AABB cost.
    // ---------------------------------------------------------------------
    struct ClusterAABB
    {
        math::Vec3 aabbMin;
        math::Vec3 aabbMax;
    };
    std::array<ClusterAABB, kClusterCount> clusterAABBs_{};

    // Fingerprint of the last (projMatrix, nearPlane, farPlane) tuple used
    // to build clusterAABBs_.  We use raw bytes + memcmp rather than glm
    // operator== because we want exact bitwise equality — the same camera
    // frame-to-frame produces bit-identical matrices, and that's exactly
    // the case we want to cache-hit on.
    float cachedProjMatrix_[16] = {};
    float cachedNearPlane_ = 0.0f;
    float cachedFarPlane_ = 0.0f;
    bool clusterCacheValid_ = false;
    bool clusterGeometryRebuiltLastFrame_ = false;

    // ---------------------------------------------------------------------
    // Texture upload dirty bits.  Each texture has its own rule:
    //   - lightDataDirty_  : bytes of lights_[0 .. lightCount_) changed.
    //   - lightGridDirty_  : either the light buffer OR cluster geometry
    //                        changed.  When set, lightIndexDirty_ is too
    //                        (they must remain coherent on the GPU — the
    //                        shader reads grid offsets to index into the
    //                        index list, and stale offsets vs. fresh
    //                        indices would mis-bind lights).
    //   - lightIndexDirty_ : OR'd in lockstep with lightGridDirty_.
    // All three latch true at first call so the initial frame uploads
    // every texture.
    // ---------------------------------------------------------------------
    bool lightDataDirty_ = true;
    bool lightGridDirty_ = true;
    bool lightIndexDirty_ = true;

    // Fingerprint of the live portion of lights_[] from the previous
    // update().  64-bit FNV-1a (cheap, collision-resistant enough for our
    // 16 KB worst case).  Counts together with lightCount_.
    uint64_t cachedLightHash_ = 0;
    uint32_t cachedLightCount_ = 0;

    // Counts every bgfx::updateTexture2D call so tests can fence on
    // upload work without having to mock bgfx itself.
    uint32_t uploadCallCount_ = 0;

    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------

    // Collect lights from the ECS into lights_[] (capped at kMaxLights).
    // Returns the number of lights collected.
    uint32_t collectLights(ecs::Registry& reg, const math::Mat4& viewMatrix);

    // Rebuild clusterAABBs_ if the (projMatrix, nearPlane, farPlane) tuple
    // changed since the last call.  Returns true when the cache was
    // refreshed (i.e. cluster geometry is dirty this frame).
    bool rebuildClusterGeometryIfDirty(float nearPlane, float farPlane,
                                       const math::Mat4& projMatrix);

    // Build grid_[] and indices_[] from lights_[] using the (possibly
    // cached) clusterAABBs_.
    void assignLightsToClusters();

    // Upload lights_[], grid_[], and indices_[] to their GPU textures, but
    // only the ones whose dirty bit is set; clears the bit afterwards.
    void uploadTextures();
};

}  // namespace engine::rendering
