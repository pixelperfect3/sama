#pragma once

#include <bgfx/bgfx.h>

#include <memory_resource>

#include "engine/ecs/Registry.h"
#include "engine/rendering/RenderResources.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// InstanceBufferBuildSystem — Phase 5 (GPU instanced mesh rendering)
//
// Reads:  InstancedMeshComponent, WorldTransformComponent, VisibleTag
// Writes: nothing in ECS
//
// Per frame:
//   1. Collect all entities with InstancedMeshComponent + WorldTransformComponent.
//   2. For each instanceGroupId, conservatively keep the group if at least one
//      entity in the group has VisibleTag (coarse cull — per-instance GPU-driven
//      culling is a future task).
//   3. Allocate one bgfx::TransientVertexBuffer (or InstanceDataBuffer) per group,
//      sized at 64 bytes per instance (one column-major Mat4).
//   4. Fill the buffer with world matrices from WorldTransformComponent.
//   5. Bind the mesh VBs / IB from RenderResources and submit one draw call per
//      group to kViewOpaque.
//
// Falls back to individual (non-instanced) draw calls when the Noop renderer is
// active or BGFX_CAPS_INSTANCING is not supported.
//
// Single-threaded encoder — multi-threaded encoder support is a Phase 6+ task.
// ---------------------------------------------------------------------------

class InstanceBufferBuildSystem
{
public:
    /// @param arena  Optional per-frame memory resource (e.g. from FrameArena).
    ///                When non-null, temporary per-frame vectors use bump allocation
    ///                instead of the heap.  Pass nullptr to fall back to the default
    ///                allocator (std::allocator / new/delete).
    void update(ecs::Registry& reg, const RenderResources& res, bgfx::ProgramHandle program,
                std::pmr::memory_resource* arena = nullptr);
};

}  // namespace engine::rendering
