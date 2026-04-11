#pragma once

#include <bgfx/bgfx.h>

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
// ---------------------------------------------------------------------------

class SkyboxRenderer
{
public:
    void init();
    void shutdown();

    // Submit one fullscreen skybox draw on the given view, sampling
    // `cubemap`. The view's transform must already be set (the skybox
    // shader reads u_view + u_proj).
    void render(bgfx::ViewId viewId, bgfx::TextureHandle cubemap);

    bool isValid() const noexcept
    {
        return bgfx::isValid(program_);
    }

private:
    bgfx::VertexBufferHandle vbh_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_skybox_ = BGFX_INVALID_HANDLE;
};

}  // namespace engine::rendering
