#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/math/Config.h"

namespace engine::math
{

// ---------------------------------------------------------------------------
// Floating-point vectors
// ---------------------------------------------------------------------------

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;

// ---------------------------------------------------------------------------
// Integer vectors  (screen coordinates, texel addressing, grid indices)
// ---------------------------------------------------------------------------

using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using IVec4 = glm::ivec4;

// ---------------------------------------------------------------------------
// Unsigned integer vectors  (texture dimensions, counts)
// ---------------------------------------------------------------------------

using UVec2 = glm::uvec2;
using UVec3 = glm::uvec3;

// ---------------------------------------------------------------------------
// Matrices
// ---------------------------------------------------------------------------

using Mat3 = glm::mat3;
using Mat4 = glm::mat4;

// ---------------------------------------------------------------------------
// Quaternion
// ---------------------------------------------------------------------------

using Quat = glm::quat;

// ---------------------------------------------------------------------------
// Colour  (RGBA, components in [0, 1])
// Kept as a distinct alias so intent is clear even though the underlying type
// is Vec4.  Do not use Vec4 for colours or Color for positions.
// ---------------------------------------------------------------------------

using Color = glm::vec4;

}  // namespace engine::math
