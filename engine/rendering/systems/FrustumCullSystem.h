#pragma once

#include "engine/ecs/Registry.h"
#include "engine/math/Frustum.h"
#include "engine/rendering/RenderResources.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// FrustumCullSystem
//
// Reads:  WorldTransformComponent, MeshComponent
// Writes: VisibleTag (add / remove)
//
// For each entity that has both WorldTransformComponent and MeshComponent:
//   1. Retrieve the Mesh AABB from RenderResources using the mesh ID.
//   2. Transform the AABB center by the world matrix (approximate: transform
//      center point, then add the original local half-extents as a conservative
//      radius).  This avoids transforming all 8 corners while remaining
//      conservative: no visible entity will be culled, though some near the
//      frustum boundary may be kept when they could have been dropped.
//   3. Call frustum.containsAABB() with the world-space min/max.
//   4. If visible: emplace VisibleTag (no-op if already present).
//      If not visible: remove VisibleTag (no-op if not present).
//
// Phase 2: single-threaded.  Phase 3 will partition the entity list across
// thread-pool workers using bgfx encoders.
// ---------------------------------------------------------------------------

class FrustumCullSystem
{
public:
    void update(ecs::Registry& reg, const RenderResources& res, const math::Frustum& frustum);
};

}  // namespace engine::rendering
