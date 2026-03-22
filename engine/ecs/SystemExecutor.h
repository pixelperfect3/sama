#pragma once

#include <cstdint>
#include <tuple>

#include "../threading/ThreadPool.h"
#include "Registry.h"
#include "Schedule.h"
#include "System.h"

namespace engine::ecs
{

// ---------------------------------------------------------------------------
// SystemExecutor<Systems...>
// Drives the compile-time Schedule against a ThreadPool each frame.
//
// Usage:
//   SystemExecutor<InputSys, MoveSys, RenderSys> executor(threadCount);
//   executor.runFrame(registry, deltaTime);
//
// Each call to runFrame iterates the pre-built phase array:
//   - All systems in a phase are dispatched to the thread pool in parallel.
//   - waitAll() is called between phases to enforce ordering.
//   - Systems in the same phase are guaranteed conflict-free by buildSchedule.
// ---------------------------------------------------------------------------

template <SystemType... Systems>
class SystemExecutor
{
public:
    explicit SystemExecutor(uint32_t threadCount) : threadPool_(threadCount) {}

    // Run one frame. Dispatches systems phase by phase.
    void runFrame(Registry& reg, float dt)
    {
        for (uint8_t p = 0; p < kSchedule_.phaseCount; ++p)
        {
            const Phase& phase = kSchedule_.phases[p];

            if (phase.count == 1)
            {
                // Single system in phase — run inline, skip thread dispatch overhead
                dispatchSystem(phase.systemIndices[0], reg, dt);
            }
            else
            {
                for (uint8_t s = 0; s < phase.count; ++s)
                {
                    const uint8_t idx = phase.systemIndices[s];
                    threadPool_.submit([this, &reg, dt, idx] { dispatchSystem(idx, reg, dt); });
                }
                threadPool_.waitAll();
            }
        }
    }

    [[nodiscard]] uint32_t threadCount() const noexcept
    {
        return threadPool_.threadCount();
    }

    // Direct access to a system instance (e.g. for configuration).
    template <typename S>
    [[nodiscard]] S& getSystem() noexcept
    {
        return std::get<S>(systems_);
    }

    template <typename S>
    [[nodiscard]] const S& getSystem() const noexcept
    {
        return std::get<S>(systems_);
    }

private:
    // Dispatch system at tuple index idx to update.
    // Uses a compile-time index walk via fold expression.
    void dispatchSystem(uint8_t idx, Registry& reg, float dt)
    {
        dispatchImpl(idx, reg, dt, std::index_sequence_for<Systems...>{});
    }

    template <std::size_t... Is>
    void dispatchImpl(uint8_t idx, Registry& reg, float dt, std::index_sequence<Is...>)
    {
        ((idx == Is ? (std::get<Is>(systems_).update(reg, dt), void()) : void()), ...);
    }

    static constexpr Schedule kSchedule_ = buildSchedule<Systems...>();

    std::tuple<Systems...> systems_;
    engine::threading::ThreadPool threadPool_;
};

}  // namespace engine::ecs
