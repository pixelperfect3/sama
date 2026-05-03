#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/audio/AudioComponents.h"
#include "engine/audio/AudioSystem.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "tests/audio/FakeAudioEngine.h"

using namespace engine::audio;
using namespace engine::ecs;
using namespace engine::rendering;
using namespace engine::math;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Mat4 translationMatrix(Vec3 pos)
{
    return glm::translate(Mat4(1.0f), pos);
}

// ---------------------------------------------------------------------------
// Listener tests
// ---------------------------------------------------------------------------

TEST_CASE("Listener position extracted from WorldTransformComponent", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    EntityID listener = reg.createEntity();
    reg.emplace<AudioListenerComponent>(listener);

    Vec3 listenerPos{5.0f, 10.0f, -3.0f};
    reg.emplace<WorldTransformComponent>(listener,
                                         WorldTransformComponent{translationMatrix(listenerPos)});

    sys.update(reg);

    CHECK(fake.listenerPositionSet);
    CHECK(std::abs(fake.lastListenerPos.x - 5.0f) < 1e-4f);
    CHECK(std::abs(fake.lastListenerPos.y - 10.0f) < 1e-4f);
    CHECK(std::abs(fake.lastListenerPos.z - (-3.0f)) < 1e-4f);
}

TEST_CASE("Listener orientation extracted from WorldTransformComponent", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    EntityID listener = reg.createEntity();
    reg.emplace<AudioListenerComponent>(listener);

    // Identity matrix: forward = -Z = (0,0,-1), up = Y = (0,1,0)
    reg.emplace<WorldTransformComponent>(listener, WorldTransformComponent{Mat4(1.0f)});

    sys.update(reg);

    CHECK(fake.listenerOrientationSet);
    CHECK(std::abs(fake.lastListenerForward.x - 0.0f) < 1e-4f);
    CHECK(std::abs(fake.lastListenerForward.y - 0.0f) < 1e-4f);
    CHECK(std::abs(fake.lastListenerForward.z - (-1.0f)) < 1e-4f);
    CHECK(std::abs(fake.lastListenerUp.x - 0.0f) < 1e-4f);
    CHECK(std::abs(fake.lastListenerUp.y - 1.0f) < 1e-4f);
    CHECK(std::abs(fake.lastListenerUp.z - 0.0f) < 1e-4f);
}

TEST_CASE("Multiple listeners: highest priority wins", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    EntityID lowPri = reg.createEntity();
    reg.emplace<AudioListenerComponent>(lowPri, AudioListenerComponent{10, {}});
    reg.emplace<WorldTransformComponent>(
        lowPri, WorldTransformComponent{translationMatrix(Vec3{1.0f, 0.0f, 0.0f})});

    EntityID highPri = reg.createEntity();
    reg.emplace<AudioListenerComponent>(highPri, AudioListenerComponent{50, {}});
    reg.emplace<WorldTransformComponent>(
        highPri, WorldTransformComponent{translationMatrix(Vec3{99.0f, 0.0f, 0.0f})});

    sys.update(reg);

    CHECK(fake.listenerPositionSet);
    CHECK(std::abs(fake.lastListenerPos.x - 99.0f) < 1e-4f);
}

TEST_CASE("No listener: no crash, no listener calls", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    // No listener entity at all.
    sys.update(reg);

    CHECK_FALSE(fake.listenerPositionSet);
    CHECK_FALSE(fake.listenerOrientationSet);
    // update3dAudio should still be called.
    CHECK(fake.update3dAudioCallCount == 1);
}

// ---------------------------------------------------------------------------
// Source tests
// ---------------------------------------------------------------------------

TEST_CASE("Auto-play triggers play3D and stores handle", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    EntityID source = reg.createEntity();
    AudioSourceComponent src{};
    src.clipId = 42;
    src.volume = 0.8f;
    src.flags = 0x04;  // autoPlay
    src.category = SoundCategory::SFX;
    reg.emplace<AudioSourceComponent>(source, src);

    Vec3 sourcePos{3.0f, 4.0f, 5.0f};
    reg.emplace<WorldTransformComponent>(source,
                                         WorldTransformComponent{translationMatrix(sourcePos)});

    sys.update(reg);

    // play3D should have been called.
    REQUIRE(fake.playCalls.size() == 1);
    CHECK(fake.playCalls[0].clipId == 42);
    CHECK(fake.playCalls[0].is3D);
    CHECK(std::abs(fake.playCalls[0].pos.x - 3.0f) < 1e-4f);
    CHECK(std::abs(fake.playCalls[0].pos.y - 4.0f) < 1e-4f);
    CHECK(std::abs(fake.playCalls[0].pos.z - 5.0f) < 1e-4f);
    CHECK(std::abs(fake.playCalls[0].volume - 0.8f) < 1e-4f);

    // busHandle should be stored, autoPlay cleared.
    auto* updatedSrc = reg.get<AudioSourceComponent>(source);
    REQUIRE(updatedSrc != nullptr);
    CHECK(updatedSrc->busHandle != 0);
    CHECK((updatedSrc->flags & 0x04) == 0);  // autoPlay cleared
    CHECK((updatedSrc->flags & 0x08) != 0);  // playing set
}

TEST_CASE("Source position updated for playing sounds", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    EntityID source = reg.createEntity();
    AudioSourceComponent src{};
    src.clipId = 1;
    src.busHandle = 5;  // already playing
    src.volume = 0.5f;
    src.pitch = 1.2f;
    src.flags = 0x08;  // playing
    reg.emplace<AudioSourceComponent>(source, src);

    Vec3 sourcePos{10.0f, 20.0f, 30.0f};
    reg.emplace<WorldTransformComponent>(source,
                                         WorldTransformComponent{translationMatrix(sourcePos)});

    // Mark handle 5 as playing in the fake engine.
    fake.playingHandles.push_back(5);

    sys.update(reg);

    // setPosition should have been called.
    REQUIRE(fake.setPositionCalls.size() == 1);
    CHECK(fake.setPositionCalls[0].handle == 5);
    CHECK(std::abs(fake.setPositionCalls[0].pos.x - 10.0f) < 1e-4f);
    CHECK(std::abs(fake.setPositionCalls[0].pos.y - 20.0f) < 1e-4f);
    CHECK(std::abs(fake.setPositionCalls[0].pos.z - 30.0f) < 1e-4f);

    // setVolume and setPitch should also be called.
    REQUIRE(fake.setVolumeCalls.size() == 1);
    CHECK(std::abs(fake.setVolumeCalls[0].volume - 0.5f) < 1e-4f);
    REQUIRE(fake.setPitchCalls.size() == 1);
    CHECK(std::abs(fake.setPitchCalls[0].pitch - 1.2f) < 1e-4f);
}

TEST_CASE("Playback completion resets busHandle", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    EntityID source = reg.createEntity();
    AudioSourceComponent src{};
    src.clipId = 1;
    src.busHandle = 7;  // was playing
    src.flags = 0x08;   // playing flag set
    reg.emplace<AudioSourceComponent>(source, src);

    reg.emplace<WorldTransformComponent>(source, WorldTransformComponent{Mat4(1.0f)});

    // Handle 7 is NOT in playingHandles, so isPlaying returns false.
    sys.update(reg);

    auto* updatedSrc = reg.get<AudioSourceComponent>(source);
    REQUIRE(updatedSrc != nullptr);
    CHECK(updatedSrc->busHandle == 0);
    CHECK((updatedSrc->flags & 0x08) == 0);  // playing cleared
}

TEST_CASE("No sources: clean no-op", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    // Add a listener but no sources.
    EntityID listener = reg.createEntity();
    reg.emplace<AudioListenerComponent>(listener);
    reg.emplace<WorldTransformComponent>(listener, WorldTransformComponent{Mat4(1.0f)});

    sys.update(reg);

    CHECK(fake.playCalls.empty());
    CHECK(fake.setPositionCalls.empty());
    CHECK(fake.update3dAudioCallCount == 1);
}

TEST_CASE("update3dAudio called every frame", "[audio]")
{
    FakeAudioEngine fake;
    AudioSystem sys(fake);
    Registry reg;

    sys.update(reg);
    sys.update(reg);
    sys.update(reg);

    CHECK(fake.update3dAudioCallCount == 3);
}

// ---------------------------------------------------------------------------
// setPauseAll plumbing — exercised by the mobile lifecycle handlers
// (Engine::handleAndroidCmd APP_CMD_PAUSE/RESUME, iOS applicationWillResign).
// FakeAudioEngine records the call so we can assert the contract.
// ---------------------------------------------------------------------------

TEST_CASE("FakeAudioEngine records setPauseAll calls", "[audio]")
{
    FakeAudioEngine fake;
    CHECK(fake.setPauseAllCallCount == 0);

    fake.setPauseAll(true);
    CHECK(fake.setPauseAllCallCount == 1);
    CHECK(fake.lastPausedState == true);

    fake.setPauseAll(false);
    CHECK(fake.setPauseAllCallCount == 2);
    CHECK(fake.lastPausedState == false);
}
