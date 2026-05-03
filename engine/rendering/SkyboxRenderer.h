#pragma once

#include <memory>

#include "engine/rendering/HandleTypes.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// SkyboxRenderer
//
// Draws a fullscreen-cube skybox sampling a TextureCube. Designed to be
// rendered on the same view as the opaque pass, AFTER opaque geometry,
// with depth test = LESS_EQUAL and depth write = OFF, so it only fills
// pixels where the depth buffer is still at the far plane.
//
// Usage:
//
//   SkyboxRenderer sky;
//   sky.init();   // creates the cube vertex/index buffers + program
//
//   // After opaque draw calls on the same view:
//   sky.render(viewId, cubemap);
//
//   sky.shutdown();
//
// The vertex shader strips translation from u_view, so the skybox always
// stays anchored to the camera regardless of where it moves. The fragment
// shader samples the cubemap by the cube's local-space position (which
// after the transform represents a direction from the camera into the
// world) and applies a Reinhard tonemap.
//
// All bgfx-typed members live behind a private Impl (pImpl) so this header
// stays bgfx-free.
// ---------------------------------------------------------------------------

class SkyboxRenderer
{
public:
    SkyboxRenderer();
    ~SkyboxRenderer();

    SkyboxRenderer(const SkyboxRenderer&) = delete;
    SkyboxRenderer& operator=(const SkyboxRenderer&) = delete;
    SkyboxRenderer(SkyboxRenderer&&) noexcept;
    SkyboxRenderer& operator=(SkyboxRenderer&&) noexcept;

    void init();
    void shutdown();

    // Submit one fullscreen skybox draw on the given view, sampling
    // `cubemap`. The view's transform must already be set (the skybox
    // shader reads u_view + u_proj).
    void render(ViewId viewId, TextureHandle cubemap);

    [[nodiscard]] bool isValid() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::rendering
