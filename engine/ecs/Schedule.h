#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "System.h"
#include "TypeList.h"

namespace engine::ecs
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

inline constexpr uint8_t kMaxSystems = 64;
inline constexpr uint8_t kMaxPhases = 64;
inline constexpr uint8_t kMaxSystemsPerPhase = 64;

// ---------------------------------------------------------------------------
// Phase — a set of systems that can run concurrently (no write conflicts)
// ---------------------------------------------------------------------------

struct Phase
{
    std::array<uint8_t, kMaxSystemsPerPhase> systemIndices{};
    uint8_t count = 0;

    constexpr void add(uint8_t idx) noexcept
    {
        systemIndices[count++] = idx;
    }
};

// ---------------------------------------------------------------------------
// Schedule — the full ordered set of phases, baked in at compile time
// ---------------------------------------------------------------------------

struct Schedule
{
    std::array<Phase, kMaxPhases> phases{};
    uint8_t phaseCount = 0;

    constexpr void addPhase(const Phase& p) noexcept
    {
        phases[phaseCount++] = p;
    }
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace detail
{

// Does system I conflict with system J?
// Conflict: I writes something J reads or writes, OR J writes something I reads.
template <typename SysI, typename SysJ>
constexpr bool conflicts() noexcept
{
    // I writes ∩ (J reads ∪ J writes)
    constexpr bool iWritesJNeeds = kIntersects<typename SysI::Writes, typename SysJ::Reads> ||
                                   kIntersects<typename SysI::Writes, typename SysJ::Writes>;
    // J writes ∩ (I reads ∪ I writes)
    constexpr bool jWritesINeeds = kIntersects<typename SysJ::Writes, typename SysI::Reads> ||
                                   kIntersects<typename SysJ::Writes, typename SysI::Writes>;
    return iWritesJNeeds || jWritesINeeds;
}

// Build a flat N×N conflict matrix as a constexpr bool array.
// conflictMatrix[i * N + j] == true means system i and system j conflict.
template <typename... Systems>
constexpr auto buildConflictMatrix() noexcept
{
    constexpr std::size_t N = sizeof...(Systems);
    std::array<bool, kMaxSystems * kMaxSystems> matrix{};

    // Use index sequence to iterate over all pairs
    [&]<std::size_t... Is>(std::index_sequence<Is...>)
    {
        (
            [&]<std::size_t I>(std::integral_constant<std::size_t, I>)
            {
                using SysI = std::tuple_element_t<I, std::tuple<Systems...>>;
                [&]<std::size_t... Js>(std::index_sequence<Js...>)
                {
                    (
                        [&]<std::size_t J>(std::integral_constant<std::size_t, J>)
                        {
                            using SysJ = std::tuple_element_t<J, std::tuple<Systems...>>;
                            if constexpr (I != J)
                            {
                                matrix[I * kMaxSystems + J] = conflicts<SysI, SysJ>();
                            }
                        }(std::integral_constant<std::size_t, Js>{}),
                        ...);
                }(std::make_index_sequence<N>{});
            }(std::integral_constant<std::size_t, Is>{}),
            ...);
    }(std::make_index_sequence<N>{});

    return matrix;
}

// Assign each system a phase level (0 = no dependencies).
// Level[i] = 1 + max(Level[j]) for all j that must run before i.
// Systems in the same level can run in parallel.
template <std::size_t N>
constexpr std::array<uint8_t, kMaxSystems> computeLevels(
    const std::array<bool, kMaxSystems * kMaxSystems>& conflict) noexcept
{
    std::array<uint8_t, kMaxSystems> levels{};

    // Iterative relaxation — converges in at most N passes for a DAG
    for (std::size_t pass = 0; pass < N; ++pass)
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            for (std::size_t j = 0; j < i; ++j)
            {
                if (conflict[j * kMaxSystems + i])
                {
                    // j must run before i — ensure i is at a strictly higher level
                    if (levels[i] <= levels[j])
                        levels[i] = static_cast<uint8_t>(levels[j] + 1);
                }
            }
        }
    }
    return levels;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// buildSchedule<Systems...>()
// Computes the full execution schedule at compile time.
// Returns a Schedule whose phases can be dispatched to a thread pool at runtime.
// ---------------------------------------------------------------------------

template <SystemType... Systems>
constexpr Schedule buildSchedule() noexcept
{
    constexpr std::size_t N = sizeof...(Systems);
    static_assert(N > 0, "buildSchedule requires at least one system");
    static_assert(N <= kMaxSystems, "Too many systems — increase kMaxSystems");

    constexpr auto conflictMatrix = detail::buildConflictMatrix<Systems...>();
    constexpr auto levels = detail::computeLevels<N>(conflictMatrix);

    // Find max level
    uint8_t maxLevel = 0;
    for (std::size_t i = 0; i < N; ++i)
        if (levels[i] > maxLevel)
            maxLevel = levels[i];

    Schedule schedule{};

    // Group systems by level into phases
    for (uint8_t lvl = 0; lvl <= maxLevel; ++lvl)
    {
        Phase phase{};
        for (std::size_t i = 0; i < N; ++i)
        {
            if (levels[i] == lvl)
                phase.add(static_cast<uint8_t>(i));
        }
        if (phase.count > 0)
            schedule.addPhase(phase);
    }

    return schedule;
}

}  // namespace engine::ecs
