#include "SoLoudAudioEngine.h"

namespace engine::audio
{

SoLoudAudioEngine::~SoLoudAudioEngine()
{
    if (initialized_)
    {
        shutdown();
    }
}

bool SoLoudAudioEngine::init(uint32_t sampleRate, uint32_t bufferSize, uint32_t maxVoices)
{
    if (initialized_)
    {
        return false;
    }

    // SoLoud::init(flags, backend, sampleRate, bufferSize, channels).
    // Use AUTO for sample rate and buffer size to let miniaudio pick optimal values.
    // channels is output channel count (1=mono, 2=stereo), NOT max voices.
    auto result = soloud_.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::MINIAUDIO,
                               SoLoud::Soloud::AUTO, SoLoud::Soloud::AUTO, 2);

    if (result != SoLoud::SO_NO_ERROR)
    {
        fprintf(stderr, "SoLoud init failed with error code: %d\n", static_cast<int>(result));
        return false;
    }

    // maxVoices controls how many sounds can play simultaneously.
    soloud_.setMaxActiveVoiceCount(maxVoices);

    // Play each category bus into the main mixer.
    for (size_t i = 0; i < kCategoryCount; ++i)
    {
        busHandles_[i] = soloud_.play(buses_[i]);
    }

    initialized_ = true;
    return true;
}

void SoLoudAudioEngine::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    clips_.clear();
    freeClipSlots_.clear();
    soloud_.deinit();
    initialized_ = false;
}

void SoLoudAudioEngine::setListenerPosition(const math::Vec3& pos)
{
    soloud_.set3dListenerPosition(pos.x, pos.y, pos.z);
}

void SoLoudAudioEngine::setListenerOrientation(const math::Vec3& forward, const math::Vec3& up)
{
    soloud_.set3dListenerAt(forward.x, forward.y, forward.z);
    soloud_.set3dListenerUp(up.x, up.y, up.z);
}

SoundHandle SoLoudAudioEngine::play(uint32_t clipId, SoundCategory category, float volume,
                                    bool loop)
{
    if (clipId == 0 || clipId > clips_.size())
    {
        return INVALID_SOUND;
    }

    auto& clip = clips_[clipId - 1];
    if (!clip)
    {
        return INVALID_SOUND;
    }

    size_t catIdx = static_cast<size_t>(category);
    if (catIdx >= kCategoryCount)
    {
        return INVALID_SOUND;
    }

    clip->setLooping(loop);
    auto handle = buses_[catIdx].play(*clip, volume);
    return static_cast<SoundHandle>(handle);
}

SoundHandle SoLoudAudioEngine::play3D(uint32_t clipId, const math::Vec3& pos,
                                      SoundCategory category, float volume, bool loop)
{
    if (clipId == 0 || clipId > clips_.size())
    {
        return INVALID_SOUND;
    }

    auto& clip = clips_[clipId - 1];
    if (!clip)
    {
        return INVALID_SOUND;
    }

    size_t catIdx = static_cast<size_t>(category);
    if (catIdx >= kCategoryCount)
    {
        return INVALID_SOUND;
    }

    clip->setLooping(loop);
    auto handle = buses_[catIdx].play3d(*clip, pos.x, pos.y, pos.z, 0.0f, 0.0f, 0.0f, volume);
    return static_cast<SoundHandle>(handle);
}

void SoLoudAudioEngine::stop(SoundHandle handle)
{
    soloud_.stop(handle);
}

void SoLoudAudioEngine::stopAll()
{
    soloud_.stopAll();
}

bool SoLoudAudioEngine::isPlaying(SoundHandle handle) const
{
    return soloud_.isValidVoiceHandle(handle);
}

void SoLoudAudioEngine::setVolume(SoundHandle handle, float volume)
{
    soloud_.setVolume(handle, volume);
}

void SoLoudAudioEngine::setPitch(SoundHandle handle, float pitch)
{
    soloud_.setRelativePlaySpeed(handle, pitch);
}

void SoLoudAudioEngine::setPosition(SoundHandle handle, const math::Vec3& pos)
{
    soloud_.set3dSourcePosition(handle, pos.x, pos.y, pos.z);
}

void SoLoudAudioEngine::setLooping(SoundHandle handle, bool loop)
{
    soloud_.setLooping(handle, loop);
}

void SoLoudAudioEngine::setCategoryVolume(SoundCategory cat, float volume)
{
    size_t catIdx = static_cast<size_t>(cat);
    if (catIdx < kCategoryCount)
    {
        soloud_.setVolume(busHandles_[catIdx], volume);
    }
}

float SoLoudAudioEngine::getCategoryVolume(SoundCategory cat) const
{
    size_t catIdx = static_cast<size_t>(cat);
    if (catIdx < kCategoryCount)
    {
        return soloud_.getVolume(busHandles_[catIdx]);
    }
    return 0.0f;
}

void SoLoudAudioEngine::setMasterVolume(float volume)
{
    soloud_.setGlobalVolume(volume);
}

float SoLoudAudioEngine::getMasterVolume() const
{
    return soloud_.getGlobalVolume();
}

uint32_t SoLoudAudioEngine::loadClip(const uint8_t* data, size_t size, bool /*streaming*/)
{
    auto wav = std::make_unique<SoLoud::Wav>();
    auto result = wav->loadMem(data, static_cast<unsigned int>(size), true, false);
    if (result != SoLoud::SO_NO_ERROR)
    {
        return 0;
    }

    uint32_t clipId;
    if (!freeClipSlots_.empty())
    {
        uint32_t slot = freeClipSlots_.back();
        freeClipSlots_.pop_back();
        clips_[slot] = std::move(wav);
        clipId = slot + 1;
    }
    else
    {
        clips_.push_back(std::move(wav));
        clipId = static_cast<uint32_t>(clips_.size());
    }

    return clipId;
}

void SoLoudAudioEngine::unloadClip(uint32_t clipId)
{
    if (clipId == 0 || clipId > clips_.size())
    {
        return;
    }

    uint32_t slot = clipId - 1;
    if (clips_[slot])
    {
        clips_[slot].reset();
        freeClipSlots_.push_back(slot);
    }
}

void SoLoudAudioEngine::update3dAudio()
{
    soloud_.update3dAudio();
}

}  // namespace engine::audio
