#pragma once

#include "TypeList.h"

namespace engine::ecs
{

class Registry;

// ---------------------------------------------------------------------------
// ISystem
// Base class for all engine systems. Concrete systems must declare:
//   using Reads  = TypeList<...>;   // components read this frame
//   using Writes = TypeList<...>;   // components written this frame
//
// These declarations are used by buildSchedule<>() to compute the compile-time
// DAG and group systems into parallel execution phases.
//
// NOTE: The returned T& from emplace<T>() can become dangling after any
// subsequent emplace<T>() on a different entity (std::vector reallocation).
// Do not hold component references across emplace calls.
//
// NOTE: Mutating components (emplace/remove/destroyEntity) inside a
// view().each() callback is unsafe — it can invalidate the view's iterators.
// Use a deferred command queue if structural changes are needed mid-iteration.
// ---------------------------------------------------------------------------

class ISystem
{
public:
    virtual ~ISystem() = default;

    virtual void update(Registry& registry, float deltaTime) = 0;
};

// ---------------------------------------------------------------------------
// SystemType concept
// Enforces that a type derives from ISystem and declares Reads + Writes.
// buildSchedule<>() requires all template parameters to satisfy this concept.
// ---------------------------------------------------------------------------

template <typename S>
concept SystemType = std::is_base_of_v<ISystem, S> && requires {
    typename S::Reads;
    typename S::Writes;
};

}  // namespace engine::ecs
