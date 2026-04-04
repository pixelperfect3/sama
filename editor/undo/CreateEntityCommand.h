#pragma once

#include "editor/undo/ICommand.h"
#include "engine/ecs/Entity.h"

// Forward declarations.
namespace engine::ecs
{
class Registry;
}

namespace engine::editor
{

class EditorState;

// ---------------------------------------------------------------------------
// CreateEntityCommand -- creates a new entity with a TransformComponent.
//
// undo() destroys the entity; redo() re-creates it (new ID).
// ---------------------------------------------------------------------------

class CreateEntityCommand final : public ICommand
{
public:
    CreateEntityCommand(ecs::Registry& registry, EditorState& state);
    ~CreateEntityCommand() override = default;

    void execute() override;
    void undo() override;
    const char* description() const override;

    [[nodiscard]] ecs::EntityID createdEntity() const
    {
        return createdEntity_;
    }

private:
    ecs::Registry& registry_;
    EditorState& state_;
    ecs::EntityID createdEntity_ = ecs::INVALID_ENTITY;
};

}  // namespace engine::editor
