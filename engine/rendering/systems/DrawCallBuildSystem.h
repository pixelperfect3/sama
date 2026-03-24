#pragma once

#include <bgfx/bgfx.h>

#include "engine/ecs/Registry.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/ShaderUniforms.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// DrawCallBuildSystem — single-threaded draw call submission.
//
// Phase 2 (unlit):
//   update(reg, res, program)
//   Reads:  VisibleTag, WorldTransformComponent, MeshComponent
//   Submits draw calls to bgfx view kViewOpaque.
//
// Phase 3 (PBR):
//   update(reg, res, program, uniforms)
//   Also reads MaterialComponent and fetches Material from RenderResources.
//   Uploads per-draw material uniforms (u_material, u_dirLight) via the
//   provided ShaderUniforms.  DirectionalLightComponent is NOT consumed here
//   — the caller is responsible for uploading u_dirLight before calling update.
//
// For each visible entity:
//   1. Fetch Mesh from RenderResources.
//   2. Set the world-space transform: bgfx::setTransform(mat4ptr).
//   3. Bind stream 0 (positions):  bgfx::setVertexBuffer(0, positionVbh).
//   4. Bind stream 1 (surface):    bgfx::setVertexBuffer(1, surfaceVbh) if valid.
//   5. Bind index buffer:          bgfx::setIndexBuffer(ibh).
//   6. Set default state:          bgfx::setState(BGFX_STATE_DEFAULT).
//   7. Submit to kViewOpaque:      bgfx::submit(kViewOpaque, program).
//
// Phase 4 will replace single-thread submission with per-thread bgfx encoders.
// ---------------------------------------------------------------------------

class DrawCallBuildSystem
{
public:
    // Phase 2 — unlit, no material uniforms.
    void update(ecs::Registry& reg, const RenderResources& res, bgfx::ProgramHandle program);

    // Phase 3 — PBR, uploads per-draw u_material uniform.
    // uniforms must not be nullptr; the caller owns the ShaderUniforms lifetime.
    // The directional light uniform (u_dirLight) must be set by the caller
    // before this call because it is frame-level, not per-draw.
    void update(ecs::Registry& reg, const RenderResources& res, bgfx::ProgramHandle program,
                ShaderUniforms* uniforms);
};

}  // namespace engine::rendering
