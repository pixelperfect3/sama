#pragma once

#include <array>
#include <glm/gtc/matrix_access.hpp>

#include "engine/math/Types.h"

namespace engine::math
{

// ---------------------------------------------------------------------------
// Frustum
//
// Built from a combined view-projection matrix (VP or MVP).
// Planes are extracted using the Gribb-Hartmann method, adapted for
// GLM_FORCE_DEPTH_ZERO_TO_ONE — bgfx/Vulkan depth range [0, 1].
//
// Each plane is stored as Vec4(nx, ny, nz, d) where a point p is inside if:
//     dot(Vec3(plane), p) + plane.w >= 0
//
// Plane normals point inward (toward the frustum interior).
// ---------------------------------------------------------------------------

class Frustum
{
public:
    // Extract the 6 frustum planes from a view-projection matrix.
    // Pass the combined VP matrix (or MVP if culling in world space).
    explicit Frustum(const Mat4& viewProj) noexcept
    {
        // Gribb-Hartmann: planes are linear combinations of the matrix rows.
        // GLM stores matrices column-major: mat[col][row].
        // Row i = (mat[0][i], mat[1][i], mat[2][i], mat[3][i]).

        const auto row = [&](int i) -> Vec4
        { return {viewProj[0][i], viewProj[1][i], viewProj[2][i], viewProj[3][i]}; };

        const Vec4 r0 = row(0);
        const Vec4 r1 = row(1);
        const Vec4 r2 = row(2);
        const Vec4 r3 = row(3);

        // For [0,1] depth (Vulkan / bgfx):
        //   Near: r2         (z >= 0)
        //   Far:  r3 - r2    (z <= w)
        planes_[kLeft] = r3 + r0;
        planes_[kRight] = r3 - r0;
        planes_[kBottom] = r3 + r1;
        planes_[kTop] = r3 - r1;
        planes_[kNear] = r2;
        planes_[kFar] = r3 - r2;

        // Normalise plane normals so distances are in world-space units.
        for (auto& p : planes_)
        {
            const float len = glm::length(Vec3(p));
            if (len > 1e-8f)
                p /= len;
        }
    }

    // Returns true if the sphere (center, radius) is fully or partially inside the frustum.
    [[nodiscard]] bool containsSphere(Vec3 center, float radius) const noexcept
    {
        for (const auto& plane : planes_)
        {
            if (glm::dot(Vec3(plane), center) + plane.w < -radius)
                return false;
        }
        return true;
    }

    // Returns true if the AABB [min, max] is fully or partially inside the frustum.
    // Uses the positive-vertex method: O(6) dot products per call.
    [[nodiscard]] bool containsAABB(Vec3 min, Vec3 max) const noexcept
    {
        for (const auto& plane : planes_)
        {
            // Positive vertex: the AABB corner most in the direction of the plane normal.
            const Vec3 positive = {
                plane.x >= 0.0f ? max.x : min.x,
                plane.y >= 0.0f ? max.y : min.y,
                plane.z >= 0.0f ? max.z : min.z,
            };
            if (glm::dot(Vec3(plane), positive) + plane.w < 0.0f)
                return false;
        }
        return true;
    }

    [[nodiscard]] const Vec4& plane(int i) const noexcept
    {
        return planes_[i];
    }

    static constexpr int kLeft = 0;
    static constexpr int kRight = 1;
    static constexpr int kBottom = 2;
    static constexpr int kTop = 3;
    static constexpr int kNear = 4;
    static constexpr int kFar = 5;

private:
    std::array<Vec4, 6> planes_{};
};

}  // namespace engine::math
