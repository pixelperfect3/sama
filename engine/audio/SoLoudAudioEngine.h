#pragma once

#include <soloud.h>
#include <soloud_bus.h>
#include <soloud_wav.h>

#include <array>
#include <memory>
#include <vector>

#include "engine/audio/IAudioEngine.h"

namespace engine::audio
{

// ---------------------------------------------------------------------------
// SoLoudAudioEngine — concrete IAudioEngine implementation backed by SoLoud
// with the miniaudio platform backend.
//
// Sounds are routed through one of four category buses (SFX, Music, UI,
// Ambient), each played into the main SoLoud mixer. Category volume is
// controlled via the bus; master volume via SoLoud's global volume.
// ---------------------------------------------------------------------------

class SoLoudAudioEngine final : public IAudioEngine
{
public:
    SoLoudAudioEngine() = default;
    ~SoLoudAudioEngine() override;

    // Lifecycle
    bool init(uint32_t sampleRate = 44100, uint32_t bufferSize = 2048,
              uint32_t maxVoices = 32) override;
    void shutdown() override;

    // Listener
    void setListenerPosition(const math::Vec3& pos) override;
    void setListenerOrientation(const math::Vec3& forward, const math::Vec3& up) override;

    // Playback
    SoundHandle play(uint32_t clipId, SoundCategory category, float volume, bool loop) override;
    SoundHandle play3D(uint32_t clipId, const math::Vec3& pos, SoundCategory category, float volume,
                       bool loop) override;
    void stop(SoundHandle handle) override;
    void stopAll() override;
    bool isPlaying(SoundHandle handle) const override;
    void setPauseAll(bool paused) override;

    // Per-voice control
    void setVolume(SoundHandle handle, float volume) override;
    void setPitch(SoundHandle handle, float pitch) override;
    void setPosition(SoundHandle handle, const math::Vec3& pos) override;
    void setLooping(SoundHandle handle, bool loop) override;

    // Category volumes
    void setCategoryVolume(SoundCategory cat, float volume) override;
    float getCategoryVolume(SoundCategory cat) const override;

    // Global
    void setMasterVolume(float volume) override;
    float getMasterVolume() const override;

    // Clip management
    uint32_t loadClip(const uint8_t* data, size_t size, bool streaming = false) override;
    void unloadClip(uint32_t clipId) override;

    // Frame update
    void update3dAudio() override;

private:
    static constexpr size_t kCategoryCount = static_cast<size_t>(SoundCategory::Count);

    mutable SoLoud::Soloud soloud_;
    std::array<SoLoud::Bus, kCategoryCount> buses_;
    std::array<unsigned int, kCategoryCount> busHandles_ = {};

    // Clip table: indexed by (clipId - 1). Null entries are free slots.
    std::vector<std::unique_ptr<SoLoud::Wav>> clips_;
    std::vector<uint32_t> freeClipSlots_;

    bool initialized_ = false;
};

}  // namespace engine::audio
