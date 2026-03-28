#include <catch2/catch_test_macros.hpp>
#include <cstddef>

#include "engine/audio/AudioComponents.h"

using namespace engine::audio;

// ---------------------------------------------------------------------------
// Layout verification
// ---------------------------------------------------------------------------

TEST_CASE("AudioSourceComponent sizeof is 28 bytes", "[audio]")
{
    static_assert(sizeof(AudioSourceComponent) == 28);
    CHECK(sizeof(AudioSourceComponent) == 28);
}

TEST_CASE("AudioSourceComponent field offsets", "[audio]")
{
    static_assert(offsetof(AudioSourceComponent, clipId) == 0);
    static_assert(offsetof(AudioSourceComponent, busHandle) == 4);
    static_assert(offsetof(AudioSourceComponent, volume) == 8);
    static_assert(offsetof(AudioSourceComponent, minDistance) == 12);
    static_assert(offsetof(AudioSourceComponent, maxDistance) == 16);
    static_assert(offsetof(AudioSourceComponent, pitch) == 20);
    static_assert(offsetof(AudioSourceComponent, category) == 24);
    static_assert(offsetof(AudioSourceComponent, flags) == 25);

    CHECK(true);  // static_asserts are the real test
}

TEST_CASE("AudioListenerComponent sizeof is 4 bytes", "[audio]")
{
    static_assert(sizeof(AudioListenerComponent) == 4);
    CHECK(sizeof(AudioListenerComponent) == 4);
}

TEST_CASE("AudioListenerComponent field offsets", "[audio]")
{
    static_assert(offsetof(AudioListenerComponent, priority) == 0);
    CHECK(true);
}

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

TEST_CASE("AudioSourceComponent default values", "[audio]")
{
    AudioSourceComponent src{};

    CHECK(src.clipId == 0);
    CHECK(src.busHandle == 0);
    CHECK(src.volume == 1.0f);
    CHECK(src.minDistance == 1.0f);
    CHECK(src.maxDistance == 100.0f);
    CHECK(src.pitch == 1.0f);
    CHECK(src.category == SoundCategory::SFX);
    CHECK(src.flags == 0);
}

TEST_CASE("AudioListenerComponent default values", "[audio]")
{
    AudioListenerComponent listener{};

    CHECK(listener.priority == 0);
}

// ---------------------------------------------------------------------------
// SoundCategory enum values
// ---------------------------------------------------------------------------

TEST_CASE("SoundCategory enum values", "[audio]")
{
    CHECK(static_cast<uint8_t>(SoundCategory::SFX) == 0);
    CHECK(static_cast<uint8_t>(SoundCategory::Music) == 1);
    CHECK(static_cast<uint8_t>(SoundCategory::UI) == 2);
    CHECK(static_cast<uint8_t>(SoundCategory::Ambient) == 3);
    CHECK(static_cast<uint8_t>(SoundCategory::Count) == 4);
}
