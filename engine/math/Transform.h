#pragma once

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "engine/math/Types.h"

namespace engine::math
{

// ---------------------------------------------------------------------------
// Transform builders
// ---------------------------------------------------------------------------

[[nodiscard]] inline Mat4 makeTranslation(Vec3 t) noexcept
{
    return glm::translate(Mat4(1.0f), t);
}

[[nodiscard]] inline Mat4 makeRotation(Quat r) noexcept
{
    return glm::mat4_cast(r);
}

[[nodiscard]] inline Mat4 makeScale(Vec3 s) noexcept
{
    return glm::scale(Mat4(1.0f), s);
}

// Compose a TRS matrix: Translation * Rotation * Scale.
// This is the standard object-to-world transform used by the scene graph.
[[nodiscard]] inline Mat4 makeTRS(Vec3 t, Quat r, Vec3 s) noexcept
{
    // Build T * R * S directly to avoid three separate matrix multiplies.
    const Mat4 rotation = glm::mat4_cast(r);
    const Mat4 translation = glm::translate(Mat4(1.0f), t);
    return translation * rotation * glm::scale(Mat4(1.0f), s);
}

// ---------------------------------------------------------------------------
// Transform decomposition
//
// Extracts translation, rotation, and scale from an existing matrix.
// Returns false if decomposition fails (e.g. degenerate matrix).
//
// Note: GLM's decompose also returns skew and perspective components which
// are unused here — we only care about TRS for standard scene graph nodes.
// ---------------------------------------------------------------------------

[[nodiscard]] inline bool decomposeTRS(const Mat4& m, Vec3& t, Quat& r, Vec3& s) noexcept
{
    Vec3 skew;
    Vec4 perspective;
    return glm::decompose(m, s, r, t, skew, perspective);
}

// ---------------------------------------------------------------------------
// Projection helpers
// ---------------------------------------------------------------------------

// Perspective projection compatible with bgfx (right-handed, [0, 1] depth).
// fovY in radians, aspect = width/height.
[[nodiscard]] inline Mat4 makePerspective(float fovY, float aspect, float zNear,
                                          float zFar) noexcept
{
    return glm::perspective(fovY, aspect, zNear, zFar);
}

// Orthographic projection compatible with bgfx ([0, 1] depth).
[[nodiscard]] inline Mat4 makeOrtho(float left, float right, float bottom, float top, float zNear,
                                    float zFar) noexcept
{
    return glm::ortho(left, right, bottom, top, zNear, zFar);
}

// Standard look-at view matrix.
[[nodiscard]] inline Mat4 makeLookAt(Vec3 eye, Vec3 center, Vec3 up) noexcept
{
    return glm::lookAt(eye, center, up);
}

}  // namespace engine::math
