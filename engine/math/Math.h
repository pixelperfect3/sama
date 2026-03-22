#pragma once

#include <cstdint>
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

#include "engine/math/Types.h"

namespace engine::math
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

inline constexpr float kPi = glm::pi<float>();
inline constexpr float kHalfPi = glm::half_pi<float>();
inline constexpr float kTwoPi = glm::two_pi<float>();
inline constexpr float kEpsilon = 1e-6f;
inline constexpr float kInfinity = std::numeric_limits<float>::infinity();

// ---------------------------------------------------------------------------
// Angle conversion
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr float toRadians(float degrees) noexcept
{
    return degrees * (kPi / 180.0f);
}

[[nodiscard]] constexpr float toDegrees(float radians) noexcept
{
    return radians * (180.0f / kPi);
}

// ---------------------------------------------------------------------------
// Scalar utilities
// ---------------------------------------------------------------------------

// Linear interpolation between a and b by t in [0, 1].
[[nodiscard]] inline float lerp(float a, float b, float t) noexcept
{
    return glm::mix(a, b, t);
}

// Clamp x to [lo, hi].
[[nodiscard]] inline float clamp(float x, float lo, float hi) noexcept
{
    return glm::clamp(x, lo, hi);
}

// Clamp x to [0, 1].
[[nodiscard]] inline float saturate(float x) noexcept
{
    return glm::clamp(x, 0.0f, 1.0f);
}

// Remap x from [inLo, inHi] to [outLo, outHi]. No clamping.
[[nodiscard]] constexpr float remap(float x, float inLo, float inHi, float outLo,
                                    float outHi) noexcept
{
    return outLo + (x - inLo) / (inHi - inLo) * (outHi - outLo);
}

// Smooth Hermite interpolation: 3t² − 2t³, with t clamped to [0, 1].
[[nodiscard]] inline float smoothstep(float lo, float hi, float x) noexcept
{
    return glm::smoothstep(lo, hi, x);
}

// ---------------------------------------------------------------------------
// Approximate equality  (for float comparisons in tests and logic)
// ---------------------------------------------------------------------------

[[nodiscard]] inline bool approxEqual(float a, float b, float epsilon = kEpsilon) noexcept
{
    return glm::abs(a - b) <= epsilon;
}

[[nodiscard]] inline bool approxEqual(Vec3 a, Vec3 b, float epsilon = kEpsilon) noexcept
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), Vec3(epsilon)));
}

[[nodiscard]] inline bool approxEqual(Vec4 a, Vec4 b, float epsilon = kEpsilon) noexcept
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), Vec4(epsilon)));
}

[[nodiscard]] inline bool approxEqual(Mat4 a, Mat4 b, float epsilon = kEpsilon) noexcept
{
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            if (!approxEqual(a[col][row], b[col][row], epsilon))
                return false;
    return true;
}

// ---------------------------------------------------------------------------
// Integer utilities
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr bool isPowerOfTwo(uint32_t x) noexcept
{
    return x > 0u && (x & (x - 1u)) == 0u;
}

[[nodiscard]] constexpr uint32_t nextPowerOfTwo(uint32_t x) noexcept
{
    if (x == 0u)
        return 1u;
    --x;
    x |= x >> 1u;
    x |= x >> 2u;
    x |= x >> 4u;
    x |= x >> 8u;
    x |= x >> 16u;
    return x + 1u;
}

}  // namespace engine::math
