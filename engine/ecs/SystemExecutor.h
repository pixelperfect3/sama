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
//
// Drives a compile-time Schedule against a ThreadPool each frame.
//
// Usage:
//   SystemExecutor<InputSys, MoveSys, RenderSys> executor(threadCount);
//   executor.runFrame(registry, deltaTime);
//
// Each call to runFrame iterates the pre-built phase array:
//   - All systems in a phase are dispatched to the thread pool in parallel.
//   - waitAll() is called between phases to enforce ordering.
//   - Systems in the same phase are guaranteed conflict-free by buildSchedule
//     (which inspects each system's Reads/Writes TypeLists at compile time
//     and refuses to put conflicting systems in the same phase).
//
// Dispatch uses ThreadPool's POD `submitTask` path — see audit item #H1 in
// docs/PERF_AUDIT_2026-05-25.md.  The argument block is heap-allocated per
// dispatch (small, ~32 bytes) and freed by the trampoline.  We could move
// this to a per-frame arena to drop the allocation entirely, but at the
// current scale (≤ kMaxSystemsPerPhase = 64 allocations per frame total)
// it's not worth the complexity.
// ---------------------------------------------------------------------------

template <SystemType... Systems>
class SystemExecutor
{
public:
    explicit SystemExecutor(uint32_t threadCount) : threadPool_(threadCount) {}

    // Run one frame.  Dispatches systems phase by phase.
    void runFrame(Registry& reg, float deltaTime)
    {
        for (uint8_t p = 0; p < kSchedule_.phaseCount; ++p)
        {
            const Phase& phase = kSchedule_.phases[p];

            if (phase.count == 1)
            {
                // Single-system phase — run inline, skip the dispatch + wait
                // round-trip entirely.  Saves ~1 µs/phase on M-series.
                dispatchSystem(phase.systemIndices[0], reg, deltaTime);
            }
            else
            {
                for (uint8_t s = 0; s < phase.count; ++s)
                {
                    auto* arg = new DispatchArg{this, &reg, deltaTime, phase.systemIndices[s]};
                    threadPool_.submitTask(&dispatchTrampoline, arg);
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
    // Heap-allocated dispatch argument — packed into the POD `submitTask`
    // arg pointer.  The trampoline frees it after running.
    struct DispatchArg
    {
        SystemExecutor* executor;
        Registry* registry;
        float deltaTime;
        uint8_t systemIndex;
    };

    static void dispatchTrampoline(void* rawArg)
    {
        auto* arg = static_cast<DispatchArg*>(rawArg);
        arg->executor->dispatchSystem(arg->systemIndex, *arg->registry, arg->deltaTime);
        delete arg;
    }

    // Dispatch the system at the runtime tuple index `idx`.  Uses a
    // compile-time fold to translate the runtime index back into a
    // `std::get<I>(systems_)` call.
    void dispatchSystem(uint8_t idx, Registry& reg, float deltaTime)
    {
        dispatchImpl(idx, reg, deltaTime, std::index_sequence_for<Systems...>{});
    }

    template <std::size_t... Is>
    void dispatchImpl(uint8_t idx, Registry& reg, float deltaTime, std::index_sequence<Is...>)
    {
        ((idx == Is ? (std::get<Is>(systems_).update(reg, deltaTime), void()) : void()), ...);
    }

    static constexpr Schedule kSchedule_ = buildSchedule<Systems...>();

    std::tuple<Systems...> systems_;
    engine::threading::ThreadPool threadPool_;
};

}  // namespace engine::ecs
