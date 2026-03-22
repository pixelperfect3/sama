#pragma once

#include <cstdint>

namespace engine::ecs
{

using EntityID = uint64_t;

inline constexpr EntityID INVALID_ENTITY = 0;

[[nodiscard]] inline constexpr uint32_t entityIndex(EntityID id) noexcept
{
    return static_cast<uint32_t>(id & 0xFFFFFFFFu);
}

[[nodiscard]] inline constexpr uint32_t entityGeneration(EntityID id) noexcept
{
    return static_cast<uint32_t>((id >> 32u) & 0xFFFFFFFFu);
}

[[nodiscard]] inline constexpr EntityID makeEntityID(uint32_t index, uint32_t generation) noexcept
{
    return (static_cast<uint64_t>(generation) << 32u) | static_cast<uint64_t>(index);
}

}  // namespace engine::ecs
