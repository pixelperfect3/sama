#pragma once

#include "engine/ecs/Registry.h"

namespace engine::audio
{

class IAudioEngine;

class AudioSystem
{
public:
    explicit AudioSystem(IAudioEngine& engine);

    // Must be called AFTER TransformSystem::update() each frame.
    void update(ecs::Registry& reg);

private:
    IAudioEngine& engine_;
};

}  // namespace engine::audio
