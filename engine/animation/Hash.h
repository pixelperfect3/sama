#pragma once
#include <cstdint>

namespace engine::animation
{
// FNV-1a 32-bit hash. Used for joint name hashing.
constexpr uint32_t fnv1a(const char* str) noexcept
{
    uint32_t hash = 2166136261u;
    while (*str)
    {
        hash ^= static_cast<uint32_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}
}  // namespace engine::animation
