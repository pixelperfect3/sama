#pragma once

#include "engine/audio/IAudioEngine.h"

namespace engine::audio
{

// ---------------------------------------------------------------------------
// NullAudioEngine — no-op implementation of IAudioEngine for headless/testing.
//
// Every method is a valid no-op: init() returns true, play() returns
// INVALID_SOUND, isPlaying() returns false, etc. This mirrors the
// NullInputBackend pattern from the input system.
// ---------------------------------------------------------------------------

class NullAudioEngine final : public IAudioEngine
{
public:
    bool init(uint32_t /*sampleRate*/ = 44100, uint32_t /*bufferSize*/ = 2048,
              uint32_t /*maxVoices*/ = 32) override
    {
        return true;
    }

    void shutdown() override {}

    void setListenerPosition(const math::Vec3& /*pos*/) override {}
    void setListenerOrientation(const math::Vec3& /*forward*/, const math::Vec3& /*up*/) override {}

    SoundHandle play(uint32_t /*clipId*/, SoundCategory /*category*/, float /*volume*/,
                     bool /*loop*/) override
    {
        return INVALID_SOUND;
    }

    SoundHandle play3D(uint32_t /*clipId*/, const math::Vec3& /*pos*/, SoundCategory /*category*/,
                       float /*volume*/, bool /*loop*/) override
    {
        return INVALID_SOUND;
    }

    void stop(SoundHandle /*handle*/) override {}
    void stopAll() override {}

    bool isPlaying(SoundHandle /*handle*/) const override
    {
        return false;
    }

    void setVolume(SoundHandle /*handle*/, float /*volume*/) override {}
    void setPitch(SoundHandle /*handle*/, float /*pitch*/) override {}
    void setPosition(SoundHandle /*handle*/, const math::Vec3& /*pos*/) override {}
    void setLooping(SoundHandle /*handle*/, bool /*loop*/) override {}

    void setCategoryVolume(SoundCategory cat, float volume) override
    {
        if (static_cast<size_t>(cat) < categoryVolumes_.size())
        {
            categoryVolumes_[static_cast<size_t>(cat)] = volume;
        }
    }

    float getCategoryVolume(SoundCategory cat) const override
    {
        if (static_cast<size_t>(cat) < categoryVolumes_.size())
        {
            return categoryVolumes_[static_cast<size_t>(cat)];
        }
        return 0.0f;
    }

    void setMasterVolume(float volume) override
    {
        masterVolume_ = volume;
    }

    float getMasterVolume() const override
    {
        return masterVolume_;
    }

    uint32_t loadClip(const uint8_t* /*data*/, size_t /*size*/, bool /*streaming*/) override
    {
        return 0;
    }

    void unloadClip(uint32_t /*clipId*/) override {}

    void update3dAudio() override {}

private:
    float masterVolume_ = 1.0f;
    std::array<float, static_cast<size_t>(SoundCategory::Count)> categoryVolumes_ = {1.0f, 1.0f,
                                                                                     1.0f, 1.0f};
};

}  // namespace engine::audio
