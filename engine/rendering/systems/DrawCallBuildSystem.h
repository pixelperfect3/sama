#pragma once

#include <bgfx/bgfx.h>

#include "engine/ecs/Registry.h"
#include "engine/rendering/RenderResources.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// DrawCallBuildSystem — Phase 2 (unlit, single-threaded)
//
// Reads:  VisibleTag, WorldTransformComponent, MeshComponent
// Submits draw calls to bgfx view kViewOpaque via bgfx::submit().
//
// For each entity with VisibleTag + WorldTransformComponent + MeshComponent:
//   1. Fetch the Mesh from RenderResources.
//   2. Set the world-space transform: bgfx::setTransform(mat4ptr).
//   3. Bind stream 0 (positions):  bgfx::setVertexBuffer(0, positionVbh).
//   4. Bind stream 1 (surface):    bgfx::setVertexBuffer(1, surfaceVbh) if valid.
//   5. Bind index buffer:          bgfx::setIndexBuffer(ibh).
//   6. Set default state:          bgfx::setState(BGFX_STATE_DEFAULT).
//   7. Submit to kViewOpaque:      bgfx::submit(kViewOpaque, program).
//
// Phase 3 will replace this with per-thread bgfx encoders for data-level
// parallelism.
// ---------------------------------------------------------------------------

class DrawCallBuildSystem
{
public:
    void update(ecs::Registry& reg, const RenderResources& res, bgfx::ProgramHandle program);
};

}  // namespace engine::rendering
