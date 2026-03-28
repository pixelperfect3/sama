#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::audio
{

// ---------------------------------------------------------------------------
// SoundCategory — routes sounds through separate mixer buses.
// ---------------------------------------------------------------------------

enum class SoundCategory : uint8_t
{
    SFX = 0,
    Music = 1,
    UI = 2,
    Ambient = 3,
    Count = 4
};

// ---------------------------------------------------------------------------
// AudioSourceComponent — attached to any entity that emits sound.
//
// References an audio clip asset and controls playback parameters.
// Fields ordered largest-alignment-first; explicit padding; static_assert on
// sizeof and offsetof catches accidental regressions at compile time.
//
// flags bit layout:
//   bit 0: loop
//   bit 1: spatial (3D)
//   bit 2: autoPlay
//   bit 3: playing
// ---------------------------------------------------------------------------

struct AudioSourceComponent  // offset  size
{
    uint32_t clipId = 0;         //  0       4   index into AudioClip table
    uint32_t busHandle = 0;      //  4       4   SoLoud voice handle (0 = not playing)
    float volume = 1.0f;         //  8       4   [0.0, 1.0]
    float minDistance = 1.0f;    // 12       4   distance at which attenuation begins
    float maxDistance = 100.0f;  // 16       4   distance at which sound is inaudible
    float pitch = 1.0f;          // 20       4   playback speed multiplier (1.0 = normal)
    SoundCategory category = SoundCategory::SFX;  // 24  1
    uint8_t flags = 0;                            // 25       1
    uint8_t _pad[2] = {};                         // 26       2
};  // total: 28 bytes
static_assert(sizeof(AudioSourceComponent) == 28);
static_assert(offsetof(AudioSourceComponent, clipId) == 0);
static_assert(offsetof(AudioSourceComponent, busHandle) == 4);
static_assert(offsetof(AudioSourceComponent, volume) == 8);
static_assert(offsetof(AudioSourceComponent, minDistance) == 12);
static_assert(offsetof(AudioSourceComponent, maxDistance) == 16);
static_assert(offsetof(AudioSourceComponent, pitch) == 20);
static_assert(offsetof(AudioSourceComponent, category) == 24);
static_assert(offsetof(AudioSourceComponent, flags) == 25);

// ---------------------------------------------------------------------------
// AudioListenerComponent — tag component placed on the entity whose world
// transform defines the audio listener position and orientation.
//
// Only one listener is active per frame. If multiple entities have this
// component, the one with the highest priority is used.
// ---------------------------------------------------------------------------

struct AudioListenerComponent  // offset  size
{
    uint8_t priority = 0;  //  0       1   higher wins
    uint8_t _pad[3] = {};  //  1       3
};  // total: 4 bytes
static_assert(sizeof(AudioListenerComponent) == 4);
static_assert(offsetof(AudioListenerComponent, priority) == 0);

}  // namespace engine::audio
