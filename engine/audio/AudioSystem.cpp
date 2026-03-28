#include "AudioSystem.h"

#include "engine/audio/AudioComponents.h"
#include "engine/audio/IAudioEngine.h"
#include "engine/rendering/EcsComponents.h"

namespace engine::audio
{

AudioSystem::AudioSystem(IAudioEngine& engine) : engine_(engine) {}

void AudioSystem::update(ecs::Registry& reg)
{
    // -----------------------------------------------------------------------
    // 1. Find the active listener entity (highest priority wins).
    // -----------------------------------------------------------------------

    bool hasListener = false;
    uint8_t bestPriority = 0;
    math::Vec3 listenerPos{0.0f};
    math::Vec3 listenerForward{0.0f, 0.0f, -1.0f};
    math::Vec3 listenerUp{0.0f, 1.0f, 0.0f};

    reg.view<AudioListenerComponent, rendering::WorldTransformComponent>().each(
        [&](ecs::EntityID /*entity*/, AudioListenerComponent& listener,
            rendering::WorldTransformComponent& wtc)
        {
            if (!hasListener || listener.priority > bestPriority)
            {
                hasListener = true;
                bestPriority = listener.priority;

                // Extract position from column 3 of the world matrix.
                listenerPos = math::Vec3(wtc.matrix[3]);

                // Forward = -Z axis (column 2, negated).
                listenerForward = -math::Vec3(wtc.matrix[2]);

                // Up = Y axis (column 1).
                listenerUp = math::Vec3(wtc.matrix[1]);
            }
        });

    if (hasListener)
    {
        engine_.setListenerPosition(listenerPos);
        engine_.setListenerOrientation(listenerForward, listenerUp);
    }

    // -----------------------------------------------------------------------
    // 2. Iterate all audio sources.
    // -----------------------------------------------------------------------

    reg.view<AudioSourceComponent, rendering::WorldTransformComponent>().each(
        [&](ecs::EntityID /*entity*/, AudioSourceComponent& src,
            rendering::WorldTransformComponent& wtc)
        {
            math::Vec3 worldPos = math::Vec3(wtc.matrix[3]);

            // (a) Auto-play: start playback if requested and not already playing.
            if ((src.flags & 0x04) != 0 && src.busHandle == 0)
            {
                bool loop = (src.flags & 0x01) != 0;
                src.busHandle =
                    engine_.play3D(src.clipId, worldPos, src.category, src.volume, loop);
                src.flags &= ~static_cast<uint8_t>(0x04);  // clear autoPlay
                src.flags |= 0x08;                         // set playing
            }
            // (b) Currently playing: update position, volume, pitch.
            else if (src.busHandle != 0 && engine_.isPlaying(src.busHandle))
            {
                engine_.setPosition(src.busHandle, worldPos);
                engine_.setVolume(src.busHandle, src.volume);
                engine_.setPitch(src.busHandle, src.pitch);
            }
            // (c) Finished playing: reset handle.
            else if (src.busHandle != 0 && !engine_.isPlaying(src.busHandle))
            {
                src.busHandle = 0;
                src.flags &= ~static_cast<uint8_t>(0x08);  // clear playing
            }
        });

    // -----------------------------------------------------------------------
    // 3. Commit 3D audio changes.
    // -----------------------------------------------------------------------

    engine_.update3dAudio();
}

}  // namespace engine::audio
