#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/Registry.h"
#include "engine/ecs/Schedule.h"
#include "engine/ecs/System.h"

using namespace engine::ecs;

// ---------------------------------------------------------------------------
// Test components
// ---------------------------------------------------------------------------

struct Position
{
    float x = 0, y = 0, z = 0;
};
struct Velocity
{
    float x = 0, y = 0, z = 0;
};
struct Health
{
    int value = 100;
};
struct Mesh
{
};
struct InputState
{
};
struct AudioSource
{
};

// ---------------------------------------------------------------------------
// Test systems
// ---------------------------------------------------------------------------

struct InputSys : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<InputState>;
    void update(Registry&, float) override {}
};

struct MoveSys : ISystem
{
    using Reads = TypeList<InputState, Velocity>;
    using Writes = TypeList<Position>;
    void update(Registry&, float) override {}
};

struct PhysicsSys : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<Position, Velocity>;
    void update(Registry&, float) override {}
};

struct RenderSys : ISystem
{
    using Reads = TypeList<Position, Mesh>;
    using Writes = TypeList<>;
    void update(Registry&, float) override {}
};

struct AudioSys : ISystem
{
    using Reads = TypeList<Position, AudioSource>;
    using Writes = TypeList<>;
    void update(Registry&, float) override {}
};

struct HealthSys : ISystem
{
    using Reads = TypeList<Health>;
    using Writes = TypeList<Health>;
    void update(Registry&, float) override {}
};

struct UnrelatedSysA : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<Health>;
    void update(Registry&, float) override {}
};

struct UnrelatedSysB : ISystem
{
    using Reads = TypeList<>;
    using Writes = TypeList<Mesh>;
    void update(Registry&, float) override {}
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("Schedule: single system produces one phase", "[schedule]")
{
    constexpr auto s = buildSchedule<InputSys>();
    STATIC_REQUIRE(s.phaseCount == 1);
    STATIC_REQUIRE(s.phases[0].count == 1);
    STATIC_REQUIRE(s.phases[0].systemIndices[0] == 0);
}

TEST_CASE("Schedule: two independent systems produce one phase (parallel)", "[schedule]")
{
    constexpr auto s = buildSchedule<RenderSys, AudioSys>();
    // Both are read-only on Position — no conflict
    STATIC_REQUIRE(s.phaseCount == 1);
    STATIC_REQUIRE(s.phases[0].count == 2);
}

TEST_CASE("Schedule: two conflicting systems produce two phases (serialized)", "[schedule]")
{
    constexpr auto s = buildSchedule<MoveSys, PhysicsSys>();
    // Both write Position — must serialize
    STATIC_REQUIRE(s.phaseCount == 2);
    STATIC_REQUIRE(s.phases[0].count == 1);
    STATIC_REQUIRE(s.phases[1].count == 1);
}

TEST_CASE("Schedule: linear chain A->B->C produces three phases", "[schedule]")
{
    // InputSys writes InputState
    // MoveSys reads InputState, writes Position
    // RenderSys reads Position
    constexpr auto s = buildSchedule<InputSys, MoveSys, RenderSys>();
    STATIC_REQUIRE(s.phaseCount == 3);
    STATIC_REQUIRE(s.phases[0].count == 1);  // InputSys
    STATIC_REQUIRE(s.phases[1].count == 1);  // MoveSys
    STATIC_REQUIRE(s.phases[2].count == 1);  // RenderSys
}

TEST_CASE("Schedule: diamond dependency collapses correctly", "[schedule]")
{
    // InputSys (phase 0)
    //   -> MoveSys (phase 1, reads InputState)
    //   -> PhysicsSys (phase 1? No — conflicts with MoveSys on Position)
    // Actually MoveSys and PhysicsSys both write Position so they serialize.
    // Input(0) -> MoveSys(1) -> Physics(2) -> Render(3), Audio(3)
    constexpr auto s = buildSchedule<InputSys, MoveSys, PhysicsSys, RenderSys, AudioSys>();
    // RenderSys and AudioSys are read-only — must be in the same (last) phase
    STATIC_REQUIRE(s.phaseCount == 4);
    STATIC_REQUIRE(s.phases[3].count == 2);  // RenderSys + AudioSys in parallel
}

TEST_CASE("Schedule: unrelated systems (disjoint writes) run in parallel", "[schedule]")
{
    constexpr auto s = buildSchedule<UnrelatedSysA, UnrelatedSysB>();
    // A writes Health, B writes Mesh — no overlap
    STATIC_REQUIRE(s.phaseCount == 1);
    STATIC_REQUIRE(s.phases[0].count == 2);
}

TEST_CASE("Schedule: read-read on same component is not a conflict", "[schedule]")
{
    // Two systems that only read Position — should be in same phase
    struct ReaderA : ISystem
    {
        using Reads = TypeList<Position>;
        using Writes = TypeList<>;
        void update(Registry&, float) override {}
    };
    struct ReaderB : ISystem
    {
        using Reads = TypeList<Position>;
        using Writes = TypeList<>;
        void update(Registry&, float) override {}
    };
    constexpr auto s = buildSchedule<ReaderA, ReaderB>();
    STATIC_REQUIRE(s.phaseCount == 1);
    STATIC_REQUIRE(s.phases[0].count == 2);
}

TEST_CASE("Schedule: write-read conflict detected (reader after writer)", "[schedule]")
{
    constexpr auto s = buildSchedule<MoveSys, RenderSys>();
    // MoveSys writes Position, RenderSys reads Position
    STATIC_REQUIRE(s.phaseCount == 2);
}

TEST_CASE("Schedule: system with empty reads/writes has no conflicts", "[schedule]")
{
    struct NopSys : ISystem
    {
        using Reads = TypeList<>;
        using Writes = TypeList<>;
        void update(Registry&, float) override {}
    };
    constexpr auto s = buildSchedule<NopSys, MoveSys>();
    // NopSys touches nothing — parallel with everything
    STATIC_REQUIRE(s.phaseCount == 1);
    STATIC_REQUIRE(s.phases[0].count == 2);
}

TEST_CASE("Schedule: system indices map back to correct registration order", "[schedule]")
{
    constexpr auto s = buildSchedule<InputSys, MoveSys>();
    // InputSys is index 0, MoveSys is index 1
    STATIC_REQUIRE(s.phases[0].systemIndices[0] == 0);  // InputSys first
    STATIC_REQUIRE(s.phases[1].systemIndices[0] == 1);  // MoveSys second
}
