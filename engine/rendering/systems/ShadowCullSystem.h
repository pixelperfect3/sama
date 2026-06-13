#pragma once

#include <cstdint>
#include <span>

#include "engine/ecs/Registry.h"
#include "engine/math/Frustum.h"
#include "engine/rendering/RenderResources.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// ShadowCullSystem
//
// Reads:  WorldTransformComponent, MeshComponent
// Writes: ShadowVisibleTag (cascadeMask bit per cascade)
//
// For each entity that has WorldTransformComponent and MeshComponent:
//   1. Retrieve the Mesh AABB from RenderResources.
//   2. Transform into world space (same conservative approximation as
//      FrustumCullSystem — see CullHelpers.h).
//   3. Test against the shadow frustum(s).
//   4. If inside cascade `i`: set bit `baseCascadeIdx + i` in
//      `ShadowVisibleTag::cascadeMask` (emplacing the tag if not already
//      present).  If outside: clear that bit; remove the tag if
//      `cascadeMask` becomes 0 (so DrawCallBuildSystem can skip with a
//      cheaper view query).
//
// Two overloads:
//
//   update(reg, res, frustum, cascadeIndex)
//       The historical per-cascade form.  3 cascades = 3 calls = 3 walks
//       of the mesh view.  Kept for backwards compatibility — and because
//       it's the right shape when each cascade is built lazily as the
//       light pass discovers it needs more depth.
//
//   update(reg, res, span<const Frustum>, baseCascadeIdx)
//       Single-pass multi-cascade form.  Walks the mesh view once,
//       computes the world AABB once, then tests against every frustum
//       in `cascadeFrustums` and accumulates the mask in a local before
//       writing back.  Bits in the written mask occupy positions
//       `baseCascadeIdx + i` for the i-th element of the span.  Audit
//       item line 43 — cuts shadow cull cost by roughly (N - 1) / N for
//       N cascades (the AABB build and view iteration both amortise).
// ---------------------------------------------------------------------------

class ShadowCullSystem
{
public:
    // Per-cascade overload — back-compat for callers that build cascade
    // frustums one at a time.
    void update(ecs::Registry& reg, const RenderResources& res, const math::Frustum& shadowFrustum,
                uint32_t cascadeIndex);

    // Multi-cascade overload — single pass over the mesh view.  The i-th
    // frustum in `cascadeFrustums` targets cascade bit `baseCascadeIdx + i`
    // in `ShadowVisibleTag::cascadeMask`.  Default `baseCascadeIdx = 0` so
    // typical callers pass `span<Frustum>{cascade0, cascade1, cascade2}`
    // and get bits 0/1/2 set.
    //
    // CONTRACT: this overload owns the entire cascadeMask range it covers
    // — every entity's bits in `[baseCascadeIdx, baseCascadeIdx + N)` are
    // recomputed.  Bits outside that range are preserved.  This lets a
    // caller drive cascades 0..2 in bulk while still calling the
    // per-cascade overload separately for, say, a spot-light shadow
    // sharing the same ShadowVisibleTag.  Don't mix the two overloads
    // with overlapping cascade indices in the same frame — the second
    // call will overwrite the first.
    void update(ecs::Registry& reg, const RenderResources& res,
                std::span<const math::Frustum> cascadeFrustums, uint32_t baseCascadeIdx = 0);
};

}  // namespace engine::rendering
