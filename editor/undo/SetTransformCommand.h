#pragma once

#include "editor/undo/ICommand.h"
#include "engine/ecs/Entity.h"
#include "engine/rendering/EcsComponents.h"

// Forward declarations.
namespace engine::ecs
{
class Registry;
}

namespace engine::editor
{

// ---------------------------------------------------------------------------
// SetTransformCommand -- undoable transform change on an entity.
//
// Stores old and new TransformComponent values.  execute() writes the new
// transform; undo() restores the old one.  Both set the dirty flag.
// ---------------------------------------------------------------------------

class SetTransformCommand final : public ICommand
{
public:
    SetTransformCommand(ecs::Registry& registry, ecs::EntityID entity,
                        const rendering::TransformComponent& oldTransform,
                        const rendering::TransformComponent& newTransform);
    ~SetTransformCommand() override = default;

    void execute() override;
    void undo() override;
    const char* description() const override;

private:
    ecs::Registry& registry_;
    ecs::EntityID entity_;
    rendering::TransformComponent oldTransform_;
    rendering::TransformComponent newTransform_;
};

}  // namespace engine::editor
