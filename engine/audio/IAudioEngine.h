#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/audio/AudioComponents.h"
#include "engine/math/Types.h"

namespace engine::audio
{

using SoundHandle = uint32_t;
inline constexpr SoundHandle INVALID_SOUND = 0;

class IAudioEngine
{
public:
    virtual ~IAudioEngine() = default;

    // Lifecycle
    virtual bool init(uint32_t sampleRate = 44100, uint32_t bufferSize = 2048,
                      uint32_t maxVoices = 32) = 0;
    virtual void shutdown() = 0;

    // Listener (one active listener per frame)
    virtual void setListenerPosition(const math::Vec3& pos) = 0;
    virtual void setListenerOrientation(const math::Vec3& forward, const math::Vec3& up) = 0;

    // Playback
    virtual SoundHandle play(uint32_t clipId, SoundCategory category = SoundCategory::SFX,
                             float volume = 1.0f, bool loop = false) = 0;
    virtual SoundHandle play3D(uint32_t clipId, const math::Vec3& pos,
                               SoundCategory category = SoundCategory::SFX, float volume = 1.0f,
                               bool loop = false) = 0;
    virtual void stop(SoundHandle handle) = 0;
    virtual void stopAll() = 0;
    virtual bool isPlaying(SoundHandle handle) const = 0;

    // Per-voice control
    virtual void setVolume(SoundHandle handle, float volume) = 0;
    virtual void setPitch(SoundHandle handle, float pitch) = 0;
    virtual void setPosition(SoundHandle handle, const math::Vec3& pos) = 0;
    virtual void setLooping(SoundHandle handle, bool loop) = 0;

    // Category volumes (master mix)
    virtual void setCategoryVolume(SoundCategory cat, float volume) = 0;
    virtual float getCategoryVolume(SoundCategory cat) const = 0;

    // Global
    virtual void setMasterVolume(float volume) = 0;
    virtual float getMasterVolume() const = 0;

    // Audio clip management
    virtual uint32_t loadClip(const uint8_t* data, size_t size, bool streaming = false) = 0;
    virtual void unloadClip(uint32_t clipId) = 0;

    // Frame update (called by AudioSystem after updating positions)
    virtual void update3dAudio() = 0;
};

}  // namespace engine::audio
