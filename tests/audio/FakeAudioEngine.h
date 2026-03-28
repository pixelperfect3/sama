#pragma once

#include <utility>
#include <vector>

#include "engine/audio/IAudioEngine.h"

namespace engine::audio
{

// ---------------------------------------------------------------------------
// FakeAudioEngine — mock IAudioEngine that records all calls for test
// inspection. Returns configurable values for isPlaying() etc.
// ---------------------------------------------------------------------------

class FakeAudioEngine final : public IAudioEngine
{
public:
    // -----------------------------------------------------------------------
    // Recorded call types
    // -----------------------------------------------------------------------

    struct PlayCall
    {
        uint32_t clipId;
        math::Vec3 pos;
        SoundCategory category;
        float volume;
        bool loop;
        bool is3D;
    };

    struct SetPositionCall
    {
        SoundHandle handle;
        math::Vec3 pos;
    };

    struct SetVolumeCall
    {
        SoundHandle handle;
        float volume;
    };

    struct SetPitchCall
    {
        SoundHandle handle;
        float pitch;
    };

    // -----------------------------------------------------------------------
    // Recorded state — public for direct test inspection
    // -----------------------------------------------------------------------

    std::vector<PlayCall> playCalls;
    std::vector<SetPositionCall> setPositionCalls;
    std::vector<SetVolumeCall> setVolumeCalls;
    std::vector<SetPitchCall> setPitchCalls;

    math::Vec3 lastListenerPos{0.0f};
    math::Vec3 lastListenerForward{0.0f, 0.0f, -1.0f};
    math::Vec3 lastListenerUp{0.0f, 1.0f, 0.0f};
    bool listenerPositionSet = false;
    bool listenerOrientationSet = false;

    int update3dAudioCallCount = 0;
    int stopAllCallCount = 0;

    // -----------------------------------------------------------------------
    // Configurable behavior
    // -----------------------------------------------------------------------

    // Next handle to return from play/play3D. Increments after each call.
    SoundHandle nextHandle = 1;

    // Set of handles that are currently "playing". Tests can add/remove.
    std::vector<SoundHandle> playingHandles;

    // -----------------------------------------------------------------------
    // IAudioEngine implementation
    // -----------------------------------------------------------------------

    bool init(uint32_t /*sampleRate*/, uint32_t /*bufferSize*/, uint32_t /*maxVoices*/) override
    {
        return true;
    }

    void shutdown() override {}

    void setListenerPosition(const math::Vec3& pos) override
    {
        lastListenerPos = pos;
        listenerPositionSet = true;
    }

    void setListenerOrientation(const math::Vec3& forward, const math::Vec3& up) override
    {
        lastListenerForward = forward;
        lastListenerUp = up;
        listenerOrientationSet = true;
    }

    SoundHandle play(uint32_t clipId, SoundCategory category, float volume, bool loop) override
    {
        SoundHandle h = nextHandle++;
        playCalls.push_back({clipId, math::Vec3{0.0f}, category, volume, loop, false});
        playingHandles.push_back(h);
        return h;
    }

    SoundHandle play3D(uint32_t clipId, const math::Vec3& pos, SoundCategory category, float volume,
                       bool loop) override
    {
        SoundHandle h = nextHandle++;
        playCalls.push_back({clipId, pos, category, volume, loop, true});
        playingHandles.push_back(h);
        return h;
    }

    void stop(SoundHandle handle) override
    {
        auto it = std::find(playingHandles.begin(), playingHandles.end(), handle);
        if (it != playingHandles.end())
        {
            playingHandles.erase(it);
        }
    }

    void stopAll() override
    {
        playingHandles.clear();
        stopAllCallCount++;
    }

    bool isPlaying(SoundHandle handle) const override
    {
        return std::find(playingHandles.begin(), playingHandles.end(), handle) !=
               playingHandles.end();
    }

    void setVolume(SoundHandle handle, float volume) override
    {
        setVolumeCalls.push_back({handle, volume});
    }

    void setPitch(SoundHandle handle, float pitch) override
    {
        setPitchCalls.push_back({handle, pitch});
    }

    void setPosition(SoundHandle handle, const math::Vec3& pos) override
    {
        setPositionCalls.push_back({handle, pos});
    }

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

    void update3dAudio() override
    {
        update3dAudioCallCount++;
    }

private:
    float masterVolume_ = 1.0f;
    std::array<float, static_cast<size_t>(SoundCategory::Count)> categoryVolumes_ = {1.0f, 1.0f,
                                                                                     1.0f, 1.0f};
};

}  // namespace engine::audio
