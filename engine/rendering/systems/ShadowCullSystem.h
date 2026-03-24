#pragma once

#include "engine/ecs/Registry.h"
#include "engine/math/Frustum.h"
#include "engine/rendering/RenderResources.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// ShadowCullSystem
//
// Reads:  WorldTransformComponent, MeshComponent, VisibleTag
// Writes: ShadowVisibleTag (cascadeMask bit per cascade)
//
// For each entity that has WorldTransformComponent and MeshComponent:
//   1. Retrieve the Mesh AABB from RenderResources.
//   2. Transform into world space (same conservative approximation as
//      FrustumCullSystem: transform center, add local half-extents).
//   3. Test against the shadow frustum.
//   4. If inside: set bit cascadeIndex in ShadowVisibleTag::cascadeMask
//      (emplacing the tag if not already present).
//      If outside: clear bit cascadeIndex; remove the tag if cascadeMask
//      becomes 0 (so DrawCallBuildSystem can skip with a cheaper view query).
//
// Phase 4 (single cascade): call update(reg, res, shadowFrustum, 0).
// The shadowFrustum must be built from lightProj * lightView.
// ---------------------------------------------------------------------------

class ShadowCullSystem
{
public:
    // Call once per frame per cascade after FrustumCullSystem has run.
    void update(ecs::Registry& reg, const RenderResources& res, const math::Frustum& shadowFrustum,
                uint32_t cascadeIndex);
};

}  // namespace engine::rendering
