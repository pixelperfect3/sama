#pragma once

namespace engine::ecs
{

class Registry;

class ISystem
{
public:
    virtual ~ISystem() = default;

    virtual void update(Registry& registry, float deltaTime) = 0;
};

}  // namespace engine::ecs
