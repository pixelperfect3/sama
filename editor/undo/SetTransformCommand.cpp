#include "editor/undo/SetTransformCommand.h"

#include "engine/ecs/Registry.h"

namespace engine::editor
{

SetTransformCommand::SetTransformCommand(ecs::Registry& registry, ecs::EntityID entity,
                                         const rendering::TransformComponent& oldTransform,
                                         const rendering::TransformComponent& newTransform)
    : registry_(registry), entity_(entity), oldTransform_(oldTransform), newTransform_(newTransform)
{
}

void SetTransformCommand::execute()
{
    auto* tc = registry_.get<rendering::TransformComponent>(entity_);
    if (tc)
    {
        tc->position = newTransform_.position;
        tc->rotation = newTransform_.rotation;
        tc->scale = newTransform_.scale;
        tc->flags |= 0x01;  // dirty
    }
}

void SetTransformCommand::undo()
{
    auto* tc = registry_.get<rendering::TransformComponent>(entity_);
    if (tc)
    {
        tc->position = oldTransform_.position;
        tc->rotation = oldTransform_.rotation;
        tc->scale = oldTransform_.scale;
        tc->flags |= 0x01;  // dirty
    }
}

const char* SetTransformCommand::description() const
{
    return "Set Transform";
}

}  // namespace engine::editor
