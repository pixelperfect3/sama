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

private:
    bgfx::TextureHandle lightDataTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle lightGridTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle lightIndexTex_ = BGFX_INVALID_HANDLE;

    uint32_t lightCount_ = 0;

    // Fixed-size CPU buffers — reused every frame, never heap-allocated.
    std::array<LightEntry, kMaxLights> lights_{};
    std::array<ClusterGridEntry, kClusterCount> grid_{};
    std::array<uint32_t, kMaxLightIndices> indices_{};

    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------

    // Collect lights from the ECS into lights_[] (capped at kMaxLights).
    // Returns the number of lights collected.
    uint32_t collectLights(ecs::Registry& reg, const math::Mat4& viewMatrix);

    // Build grid_[] and indices_[] from lights_[] and the camera parameters.
    void buildClusters(float nearPlane, float farPlane, uint16_t screenWidth, uint16_t screenHeight,
                       const math::Mat4& projMatrix);

    // Upload lights_[], grid_[], and indices_[] to the GPU textures.
    void uploadTextures();
};

}  // namespace engine::rendering
