#include "editor/undo/CreateEntityCommand.h"

#include "editor/EditorState.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/scene/NameComponent.h"

using namespace engine::ecs;
using namespace engine::rendering;

namespace engine::editor
{

CreateEntityCommand::CreateEntityCommand(Registry& registry, EditorState& state)
    : registry_(registry), state_(state)
{
}

void CreateEntityCommand::execute()
{
    createdEntity_ = registry_.createEntity();

    TransformComponent tc{};
    tc.position = {0.0f, 0.0f, 0.0f};
    tc.rotation = glm::quat(1, 0, 0, 0);
    tc.scale = {1.0f, 1.0f, 1.0f};
    tc.flags = 0x01;  // dirty
    registry_.emplace<TransformComponent>(createdEntity_, tc);
    registry_.emplace<WorldTransformComponent>(createdEntity_);
    registry_.emplace<engine::scene::NameComponent>(createdEntity_,
                                                    engine::scene::NameComponent{"New Entity"});

    // Select the newly created entity.
    state_.select(createdEntity_);
}

void CreateEntityCommand::undo()
{
    if (createdEntity_ != INVALID_ENTITY && registry_.isValid(createdEntity_))
    {
        // Clear selection if this entity is selected.
        if (state_.isSelected(createdEntity_))
        {
            state_.clearSelection();
        }
        registry_.destroyEntity(createdEntity_);
    }
}

const char* CreateEntityCommand::description() const
{
    return "Create Entity";
}

}  // namespace engine::editor
