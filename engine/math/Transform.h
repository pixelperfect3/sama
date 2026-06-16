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
// inverseTRS — fast inverse for TRS matrices (T * R * S).
//
// `glm::inverse(Mat4)` computes a general 4x4 inverse via cofactor expansion
// (~100 muls).  When the input is known to be an affine TRS matrix — which
// every world matrix produced by `composeLocal` is — we can compute the
// inverse directly from the matrix structure in ~30 muls.
//
// Derivation: for `M = [R*S | t; 0 0 0 1]` with R orthonormal and
// S = diag(sx, sy, sz):
//
//   M^-1 = [A^-1 | -A^-1 * t; 0 0 0 1]
//        where A^-1 = S^-1 * R^T = diag(1/s) * R^T
//
// The trick: since each column of M is `s_i * R.col_i`, we have
// |M.col_i|^2 = s_i^2.  The element-wise form:
//
//   A^-1[i, j] = (1 / s_i^2) * M[j, i]
//
// — i.e. "transpose A and divide each row by its corresponding s_i^2."
// No general inverse, no cofactor expansion, no glm::decompose.
//
// CONTRACT: the input must be an affine TRS matrix (no skew, no
// perspective).  Skew matrices give wrong results silently.  See
// docs/PERF_AUDIT_2026-05-25.md item line 125 for the audit context.
[[nodiscard]] inline Mat4 inverseTRS(const Mat4& m) noexcept
{
    // Per-column inverse squared scale.
    // |M.col_i|^2 = dot(M.col_i, M.col_i) = s_i^2 when R is orthonormal.
    const float invSx2 = 1.0f / (m[0].x * m[0].x + m[0].y * m[0].y + m[0].z * m[0].z);
    const float invSy2 = 1.0f / (m[1].x * m[1].x + m[1].y * m[1].y + m[1].z * m[1].z);
    const float invSz2 = 1.0f / (m[2].x * m[2].x + m[2].y * m[2].y + m[2].z * m[2].z);

    // A^-1 columns: row-scaled transpose of A.
    // A^-1.col_j[i] = (1 / s_i^2) * A[j, i] = (1 / s_i^2) * M.col_j[i transposed].
    // In column-major glm: M.col_j[i] = M[j][i].
    const Vec3 invCol0(m[0].x * invSx2, m[1].x * invSy2, m[2].x * invSz2);
    const Vec3 invCol1(m[0].y * invSx2, m[1].y * invSy2, m[2].y * invSz2);
    const Vec3 invCol2(m[0].z * invSx2, m[1].z * invSy2, m[2].z * invSz2);

    // Inverse translation = -A^-1 * t.  Treat A^-1 as a 3x3 with the
    // columns we just computed; t is M.col_3.xyz.
    const Vec3 translation = Vec3(m[3]);
    const Vec3 invTranslation = -(invCol0 * translation.x + invCol1 * translation.y +
                                  invCol2 * translation.z);

    return Mat4(Vec4(invCol0, 0.0f), Vec4(invCol1, 0.0f), Vec4(invCol2, 0.0f),
                Vec4(invTranslation, 1.0f));
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
