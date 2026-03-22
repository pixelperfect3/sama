#pragma once

#include "engine/math/Types.h"

namespace engine::math
{

// ---------------------------------------------------------------------------
// SIMD type stubs
//
// Today these are plain aliases to the GLM types — zero overhead, zero change
// to call sites.  When mobile profiling identifies TransformSystem world matrix
// updates as a bottleneck, replace these with ARM NEON intrinsic wrappers
// (~200–300 lines, this file only).  All call sites automatically get the
// acceleration without modification.
//
// Trigger condition: mobile profiling shows TransformSystem is a bottleneck.
// Target operations: Mat4 multiply, Vec4 arithmetic, frustum AABB dot products.
// Intrinsics to use: vld1q_f32, vmulq_f32, vmlaq_f32, vaddq_f32 (ARM NEON).
// ---------------------------------------------------------------------------

using SimdMat4 = Mat4;
using SimdVec4 = Vec4;

}  // namespace engine::math
