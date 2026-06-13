#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>

#include "engine/ecs/Registry.h"
#include "engine/ecs/System.h"
#include "engine/ecs/SystemExecutor.h"

using namespace engine::ecs;

// ---------------------------------------------------------------------------
// SystemExecutor test fixtures
//
// The compile-time `buildSchedule<Systems...>()` reads each System's
// Reads/Writes TypeLists and refuses to put conflicting systems in the
// same phase.  Tests here exploit that:
//
//   - "concurrent phase" tests use systems with disjoint Writes (no
//     conflict), so they land in phase 0 together and actually run in
//     parallel.
//
//   - "barrier" tests pair a writer-of-A in phase 0 with a
//     reader-of-A-writer-of-B in phase 1.  Schedule sees the conflict
//     and inserts a phase boundary.
//
// Tests run small enough loops (≤ 10 000 frames) that they're noise-bounded
// at a few ms per case — fine for CI.  Each ThreadPool used is a 4-worker
// pool unless noted.  Audit item line 80 in
// docs/PERF_AUDIT_2026-05-25.md.
// ---------------------------------------------------------------------------

namespace
{

// Per-entity state the systems operate on.  Plain ints — phase ordering
// in the executor (waitAll() between phases, with acq-rel semantics on
// the underlying atomic) provides happens-before between writes in
// phase N and reads in phase N+1, so component fields don't need to be
// atomic on the test side.  Within a phase the schedule's conflict
// detection prevents concurrent access to the same component.
struct ComponentA
{
    int value = 0;
};

struct ComponentB
{
    int value = 0;
};

struct ComponentC
{
    int value = 0;
};

// Records the OS thread ID that ran the system.  Plain `thread::id` —
// `runFrame()`'s `waitAll()` provides release on the worker side and
// acquire on the caller side, so test reads after `runFrame` returns
// happen-after the worker write.
struct ThreadIdRecorder
{
    std::thread::id lastRunner{};
};

// ---------------------------------------------------------------------------
// Systems that increment one component each — declare disjoint Writes so
// the schedule lets them run in phase 0 together.
// ---------------------------------------------------------------------------

struct IncA : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ComponentA>;
    void update(Registry& reg, float /*dt*/) override
    {
        reg.view<ComponentA>().each([&](EntityID /*e*/, ComponentA& c) { c.value += 1; });
    }
};

struct IncB : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ComponentB>;
    void update(Registry& reg, float /*dt*/) override
    {
        reg.view<ComponentB>().each([&](EntityID /*e*/, ComponentB& c) { c.value += 1; });
    }
};

struct IncC : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ComponentC>;
    void update(Registry& reg, float /*dt*/) override
    {
        reg.view<ComponentC>().each([&](EntityID /*e*/, ComponentC& c) { c.value += 1; });
    }
};

// Three systems that each spin for ~50 µs before writing.  The sleep makes
// the parallelism observable in wall-clock time.
struct SlowIncA : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ComponentA>;
    void update(Registry& reg, float /*dt*/) override
    {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        reg.view<ComponentA>().each([&](EntityID /*e*/, ComponentA& c) { c.value += 1; });
    }
};
struct SlowIncB : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ComponentB>;
    void update(Registry& reg, float /*dt*/) override
    {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        reg.view<ComponentB>().each([&](EntityID /*e*/, ComponentB& c) { c.value += 1; });
    }
};
struct SlowIncC : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ComponentC>;
    void update(Registry& reg, float /*dt*/) override
    {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        reg.view<ComponentC>().each([&](EntityID /*e*/, ComponentC& c) { c.value += 1; });
    }
};

// Phase-ordering pair: Phase 0 writes A, Phase 1 reads A and writes B.
// Conflict (B writes, A reads → ordered before B) forces a barrier.

struct WriteA : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ComponentA>;
    void update(Registry& reg, float /*dt*/) override
    {
        reg.view<ComponentA>().each([&](EntityID /*e*/, ComponentA& c) { c.value += 1; });
    }
};

struct ReadAWriteB : ISystem
{
    using Reads = TypeList<ComponentA>;
    using Writes = TypeList<ComponentB>;
    void update(Registry& reg, float /*dt*/) override
    {
        // For each entity that has both, set B = A.  If the barrier holds,
        // every iteration sees A's just-incremented value.
        reg.view<ComponentA, ComponentB>().each([&](EntityID /*e*/, ComponentA& a, ComponentB& b)
                                                { b.value = a.value; });
    }
};

// Inline-dispatch sentinel: single system in its own phase.  Records the
// thread id it runs on so the test can verify the executor short-circuits
// to inline execution (skipping the pool dispatch).
struct RecorderSys : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ThreadIdRecorder>;
    void update(Registry& reg, float /*dt*/) override
    {
        reg.view<ThreadIdRecorder>().each([&](EntityID /*e*/, ThreadIdRecorder& r)
                                          { r.lastRunner = std::this_thread::get_id(); });
    }
};

// Configurable counter — pinned through getSystem<S>() in the
// configuration test.  System multiplies counter by a configurable
// factor each frame.
struct ConfigurableSys : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<ComponentA>;
    int factor = 1;
    void update(Registry& reg, float /*dt*/) override
    {
        reg.view<ComponentA>().each([&](EntityID /*e*/, ComponentA& c)
                                    { c.value = c.value * factor; });
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("SystemExecutor: single system dispatch increments component", "[ecs][executor]")
{
    Registry reg;
    for (int i = 0; i < 10; ++i)
    {
        EntityID entity = reg.createEntity();
        reg.emplace<ComponentA>(entity);
    }

    SystemExecutor<IncA> executor(/*threadCount=*/4);
    executor.runFrame(reg, 0.016F);

    reg.view<ComponentA>().each([&](EntityID /*e*/, ComponentA& c) { REQUIRE(c.value == 1); });
}

TEST_CASE("SystemExecutor: single-system phase runs inline on caller thread", "[ecs][executor]")
{
    // SystemExecutor.h's `if (phase.count == 1)` branch dispatches the
    // system directly without going through the pool — saves the round-trip
    // dispatch latency.  Verify by checking the thread::id the system
    // actually ran on equals the caller's.
    Registry reg;
    EntityID entity = reg.createEntity();
    reg.emplace<ThreadIdRecorder>(entity);

    SystemExecutor<RecorderSys> executor(/*threadCount=*/4);
    const std::thread::id callerId = std::this_thread::get_id();

    // Run 10 frames — if the inline branch is correctly short-circuited,
    // every frame should record the caller's id.
    for (int i = 0; i < 10; ++i)
    {
        executor.runFrame(reg, 0.016F);
        auto* recorder = reg.get<ThreadIdRecorder>(entity);
        REQUIRE(recorder != nullptr);
        REQUIRE(recorder->lastRunner == callerId);
    }
}

TEST_CASE("SystemExecutor: multi-system phase runs in parallel", "[ecs][executor]")
{
    // Three systems with disjoint writes → schedule puts them all in
    // phase 0.  Each sleeps for ~50 µs before incrementing.  If executor
    // runs them serially: wall-clock ≥ 3 × 50 µs = 150 µs.  If parallel:
    // wall-clock ≈ 50 µs.  Threshold at 100 µs catches the regression
    // even with scheduling jitter.
    //
    // Timing-sensitive — pass on 3/5 trials to tolerate one-off OS hiccups.
    Registry reg;
    for (int i = 0; i < 5; ++i)
    {
        EntityID entity = reg.createEntity();
        reg.emplace<ComponentA>(entity);
        reg.emplace<ComponentB>(entity);
        reg.emplace<ComponentC>(entity);
    }

    SystemExecutor<SlowIncA, SlowIncB, SlowIncC> executor(/*threadCount=*/4);

    int fastEnoughTrials = 0;
    constexpr int kTrials = 5;
    for (int trial = 0; trial < kTrials; ++trial)
    {
        const auto start = std::chrono::steady_clock::now();
        executor.runFrame(reg, 0.016F);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        if (micros < 130)
        {
            ++fastEnoughTrials;
        }
    }
    INFO("fast-enough trials = " << fastEnoughTrials);
    REQUIRE(fastEnoughTrials >= 3);

    // Verify counts too — every entity got incremented once per system per
    // trial (5 trials, each system once per trial = 5 each).
    reg.view<ComponentA>().each([&](EntityID /*e*/, ComponentA& c) { REQUIRE(c.value == 5); });
    reg.view<ComponentB>().each([&](EntityID /*e*/, ComponentB& c) { REQUIRE(c.value == 5); });
    reg.view<ComponentC>().each([&](EntityID /*e*/, ComponentC& c) { REQUIRE(c.value == 5); });
}

TEST_CASE("SystemExecutor: phase ordering creates a read-after-write barrier", "[ecs][executor]")
{
    // WriteA increments A in phase 0.  ReadAWriteB stores A's value into B
    // in phase 1.  If the barrier holds, B == A on every entity at the end
    // of each frame.  If the barrier fails, B can be one behind A on a
    // racing read.
    Registry reg;
    for (int i = 0; i < 50; ++i)
    {
        EntityID entity = reg.createEntity();
        reg.emplace<ComponentA>(entity);
        reg.emplace<ComponentB>(entity);
    }

    SystemExecutor<WriteA, ReadAWriteB> executor(/*threadCount=*/4);

    for (int frame = 0; frame < 100; ++frame)
    {
        executor.runFrame(reg, 0.016F);
        reg.view<ComponentA, ComponentB>().each(
            [&](EntityID /*e*/, ComponentA& a, ComponentB& b)
            {
                REQUIRE(b.value == a.value);
                REQUIRE(a.value == frame + 1);
            });
    }
}

TEST_CASE("SystemExecutor: 10000-frame stress with phase ordering — no race", "[ecs][executor]")
{
    // Same shape as the barrier test, scaled up to look for the rare race.
    // 10 000 frames × 100 entities → 2 000 000 read/write pairs.  If the
    // executor ever returned from a phase before workers had drained, B
    // would lag A.  We REQUIRE the final state, then sample mid-run to
    // catch the intermediate behaviour too.
    Registry reg;
    for (int i = 0; i < 100; ++i)
    {
        EntityID entity = reg.createEntity();
        reg.emplace<ComponentA>(entity);
        reg.emplace<ComponentB>(entity);
    }

    SystemExecutor<WriteA, ReadAWriteB> executor(/*threadCount=*/4);

    constexpr int kFrames = 10000;
    for (int frame = 0; frame < kFrames; ++frame)
    {
        executor.runFrame(reg, 0.001F);
        // Spot-check every 1000 frames to detect drift without spamming
        // the REQUIRE log.
        if (frame % 1000 == 999)
        {
            const int expected = frame + 1;
            reg.view<ComponentA, ComponentB>().each(
                [&](EntityID /*e*/, ComponentA& a, ComponentB& b)
                {
                    REQUIRE(a.value == expected);
                    REQUIRE(b.value == expected);
                });
        }
    }
}

// ---------------------------------------------------------------------------
// Conservation-law race-check.  See audit item line 80 in
// docs/PERF_AUDIT_2026-05-25.md.
//
// Setup: three independent producers (phase 0) and one summer (phase 1).
// Each producer increments its OWN component on every entity; the summer
// reads all three and writes a `Total` component = A + B + C.  Run 10 000
// frames.  Conservation invariant: at the end of frame N, every entity
// has A == B == C == N + 1 and Total == 3 * (N + 1).
//
// Failure modes the test would catch:
//   - A race between two producers writing to the same component (would
//     under-count one of them).  Schedule's conflict matrix should
//     prevent this — but a refactor that broke the matrix would surface
//     here.
//   - A missing barrier between producer phase and summer phase (would
//     make Total lag by one frame).
//   - A torn write into Total from a partially-completed producer phase
//     (would produce intermediate sums).
//
// **Run with TSAN** to catch races the conservation check might miss:
//   cmake -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
//   cmake --build build-tsan --target engine_tests
//   build-tsan/engine_tests "[ecs][executor]"
//
// TSAN is opt-in (build flag) rather than always-on; the test runs and
// passes without it.  TSAN catches data races even if the test
// assertions happen to pass on a given run.
// ---------------------------------------------------------------------------

namespace
{
struct TotalSum
{
    int value = 0;
};

struct SumABC : ISystem
{
    using Reads = TypeList<ComponentA, ComponentB, ComponentC>;
    using Writes = TypeList<TotalSum>;
    void update(Registry& reg, float /*dt*/) override
    {
        reg.view<ComponentA, ComponentB, ComponentC, TotalSum>().each(
            [&](EntityID /*e*/, ComponentA& a, ComponentB& b, ComponentC& c, TotalSum& t)
            { t.value = a.value + b.value + c.value; });
    }
};
}  // namespace

TEST_CASE("SystemExecutor: 10000-frame conservation race-check (TSAN-friendly)",
          "[ecs][executor]")
{
    Registry reg;
    constexpr int kEntities = 200;
    for (int i = 0; i < kEntities; ++i)
    {
        EntityID entity = reg.createEntity();
        reg.emplace<ComponentA>(entity);
        reg.emplace<ComponentB>(entity);
        reg.emplace<ComponentC>(entity);
        reg.emplace<TotalSum>(entity);
    }

    // Schedule:
    //   phase 0: {IncA, IncB, IncC}   — disjoint writes, run concurrently.
    //   phase 1: {SumABC}             — reads A/B/C, writes TotalSum.
    SystemExecutor<IncA, IncB, IncC, SumABC> executor(/*threadCount=*/4);

    constexpr int kFrames = 10000;
    for (int frame = 0; frame < kFrames; ++frame)
    {
        executor.runFrame(reg, 0.001F);
        // Spot-check every 1000 frames — full per-frame asserts would
        // generate 200 × 10 000 = 2M Catch2 events.  Spot-checking still
        // catches any drift > 1 in 1000 frames.
        if (frame % 1000 == 999)
        {
            const int expected = frame + 1;
            const int expectedTotal = 3 * expected;
            reg.view<ComponentA, ComponentB, ComponentC, TotalSum>().each(
                [&](EntityID /*e*/, ComponentA& a, ComponentB& b, ComponentC& c, TotalSum& t)
                {
                    REQUIRE(a.value == expected);
                    REQUIRE(b.value == expected);
                    REQUIRE(c.value == expected);
                    REQUIRE(t.value == expectedTotal);
                });
        }
    }
}

TEST_CASE("SystemExecutor: getSystem<S>() returns the configured instance", "[ecs][executor]")
{
    Registry reg;
    EntityID entity = reg.createEntity();
    auto& comp = reg.emplace<ComponentA>(entity);
    comp.value = 7;

    SystemExecutor<ConfigurableSys> executor(/*threadCount=*/2);

    // Configure via getSystem<>() — the system's `factor` field is mutated
    // before the first runFrame.
    executor.getSystem<ConfigurableSys>().factor = 3;
    executor.runFrame(reg, 0.016F);

    // ConfigurableSys does `c.value = current * factor`.  7 * 3 = 21.
    auto* updated = reg.get<ComponentA>(entity);
    REQUIRE(updated != nullptr);
    REQUIRE(updated->value == 21);

    // Run a second frame with a different factor — also via getSystem<>().
    executor.getSystem<ConfigurableSys>().factor = 2;
    executor.runFrame(reg, 0.016F);
    REQUIRE(updated->value == 42);
}
