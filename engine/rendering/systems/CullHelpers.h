#pragma once

#include <cmath>

#include "engine/math/Types.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// CullHelpers — shared between FrustumCullSystem and ShadowCullSystem.
//
// Both systems compute a conservative world-space AABB for each mesh entity
// and test it against one or more frustums.  Pulling the AABB-build into a
// single inline helper keeps the two cull paths in lockstep (any future
// precision tweak only needs one edit) and lets the compiler inline the
// math directly into both loops.
//
// See docs/PERF_AUDIT_2026-05-25.md item line 41 for the rationale behind
// the explicit-fabsf form.
// ---------------------------------------------------------------------------

// Build a conservative world-space AABB from a local AABB + 4x4 world
// transform.  The classic AABB-rotation trick: the world-space half-extent
// in each axis is the dot product of the local half-extent with the
// per-axis absolute-value of the rotation columns.  Exact for axis-aligned
// transforms; conservative (slightly larger) for rotated ones.
//
// Old form used `glm::abs(Vec4)` three times + `Mat3` constructor (12-float
// copy) + a `Mat3 * Vec3` multiply.  This form drops to 9 fabsf + 9 muls +
// 6 adds, no temporary objects on the stack, and gives the compiler clean
// scalar code that auto-vectorises better.
//
// outMin/outMax are written by reference so the caller decides whether to
// store them in registers or local Vec3 variables (both work; the compiler
// generally elides the round-trip).
inline void computeConservativeWorldAabb(const math::Mat4& worldMatrix,
                                         const math::Vec3& localCenter,
                                         const math::Vec3& localHalfExtent, math::Vec3& outMin,
                                         math::Vec3& outMax)
{
    // Transform local center into world space.  Position-bearing column
    // of the world matrix is `worldMatrix[3]` (column-major glm::mat4); the
    // straight `Mat4 * Vec4(c, 1)` form lets glm pick the best sequence on
    // each backend without us hand-rolling it.
    const math::Vec3 worldCenter = math::Vec3(worldMatrix * math::Vec4(localCenter, 1.0F));

    // The rotation columns are worldMatrix[0..2].xyz.  Take fabsf of each
    // of the 9 scalar entries (skipping the unused .w of each Vec4) and
    // do the conservative AABB-rotate trick directly.
    const float absR00 = std::fabs(worldMatrix[0].x);
    const float absR01 = std::fabs(worldMatrix[1].x);
    const float absR02 = std::fabs(worldMatrix[2].x);
    const float absR10 = std::fabs(worldMatrix[0].y);
    const float absR11 = std::fabs(worldMatrix[1].y);
    const float absR12 = std::fabs(worldMatrix[2].y);
    const float absR20 = std::fabs(worldMatrix[0].z);
    const float absR21 = std::fabs(worldMatrix[1].z);
    const float absR22 = std::fabs(worldMatrix[2].z);

    const math::Vec3 worldHalfExtent{
        absR00 * localHalfExtent.x + absR01 * localHalfExtent.y + absR02 * localHalfExtent.z,
        absR10 * localHalfExtent.x + absR11 * localHalfExtent.y + absR12 * localHalfExtent.z,
        absR20 * localHalfExtent.x + absR21 * localHalfExtent.y + absR22 * localHalfExtent.z};

    outMin = worldCenter - worldHalfExtent;
    outMax = worldCenter + worldHalfExtent;
}

}  // namespace engine::rendering
