#include <catch2/catch_test_macros.hpp>

#include "engine/audio/NullAudioEngine.h"

using namespace engine::audio;
using namespace engine::math;

TEST_CASE("NullAudioEngine init returns true", "[audio]")
{
    NullAudioEngine engine;
    CHECK(engine.init());
}

TEST_CASE("NullAudioEngine play returns INVALID_SOUND", "[audio]")
{
    NullAudioEngine engine;
    engine.init();

    CHECK(engine.play(1, SoundCategory::SFX, 1.0f, false) == INVALID_SOUND);
    CHECK(engine.play3D(1, Vec3{0.0f}, SoundCategory::SFX, 1.0f, false) == INVALID_SOUND);
}

TEST_CASE("NullAudioEngine isPlaying returns false", "[audio]")
{
    NullAudioEngine engine;
    engine.init();

    CHECK_FALSE(engine.isPlaying(0));
    CHECK_FALSE(engine.isPlaying(1));
    CHECK_FALSE(engine.isPlaying(999));
}

TEST_CASE("NullAudioEngine no crashes on full call sequence", "[audio]")
{
    NullAudioEngine engine;
    CHECK(engine.init());

    engine.setListenerPosition(Vec3{1.0f, 2.0f, 3.0f});
    engine.setListenerOrientation(Vec3{0.0f, 0.0f, -1.0f}, Vec3{0.0f, 1.0f, 0.0f});

    auto h = engine.play(1, SoundCategory::SFX, 0.5f, true);
    auto h2 = engine.play3D(2, Vec3{5.0f}, SoundCategory::Music, 1.0f, false);
    engine.stop(h);
    engine.stopAll();
    engine.isPlaying(h2);

    engine.setVolume(h, 0.5f);
    engine.setPitch(h, 1.5f);
    engine.setPosition(h, Vec3{1.0f});
    engine.setLooping(h, true);

    engine.setCategoryVolume(SoundCategory::SFX, 0.8f);
    engine.getCategoryVolume(SoundCategory::SFX);

    engine.setMasterVolume(0.5f);
    engine.getMasterVolume();

    engine.loadClip(nullptr, 0, false);
    engine.unloadClip(1);

    engine.update3dAudio();

    engine.shutdown();

    // No crash means pass.
    CHECK(true);
}

TEST_CASE("NullAudioEngine shutdown is idempotent", "[audio]")
{
    NullAudioEngine engine;
    engine.init();
    engine.shutdown();
    engine.shutdown();
    engine.shutdown();

    CHECK(true);
}

TEST_CASE("NullAudioEngine master volume get/set", "[audio]")
{
    NullAudioEngine engine;
    engine.init();

    CHECK(engine.getMasterVolume() == 1.0f);
    engine.setMasterVolume(0.5f);
    CHECK(engine.getMasterVolume() == 0.5f);
}

TEST_CASE("NullAudioEngine category volume get/set", "[audio]")
{
    NullAudioEngine engine;
    engine.init();

    CHECK(engine.getCategoryVolume(SoundCategory::SFX) == 1.0f);
    engine.setCategoryVolume(SoundCategory::SFX, 0.3f);
    CHECK(engine.getCategoryVolume(SoundCategory::SFX) == 0.3f);

    // Other categories unchanged.
    CHECK(engine.getCategoryVolume(SoundCategory::Music) == 1.0f);
}
