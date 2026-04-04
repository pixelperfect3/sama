#pragma once

#include <string>

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

class EditorState;

// ---------------------------------------------------------------------------
// DeleteEntityCommand -- deletes an entity, storing all component data for
// undo.  Only stores TransformComponent, WorldTransformComponent,
// MeshComponent, MaterialComponent, VisibleTag, and NameComponent (the
// common set used in the editor scene).
// ---------------------------------------------------------------------------

class DeleteEntityCommand final : public ICommand
{
public:
    DeleteEntityCommand(ecs::Registry& registry, EditorState& state, ecs::EntityID entity);
    ~DeleteEntityCommand() override = default;

    void execute() override;
    void undo() override;
    const char* description() const override;

private:
    ecs::Registry& registry_;
    EditorState& state_;
    ecs::EntityID entity_;

    // Stored component data for undo restoration.
    bool hadTransform_ = false;
    rendering::TransformComponent savedTransform_{};

    bool hadWorldTransform_ = false;
    rendering::WorldTransformComponent savedWorldTransform_{};

    bool hadMesh_ = false;
    rendering::MeshComponent savedMesh_{};

    bool hadMaterial_ = false;
    rendering::MaterialComponent savedMaterial_{};

    bool hadVisible_ = false;

    bool hadName_ = false;
    std::string savedName_;

    bool hadDirLight_ = false;
    rendering::DirectionalLightComponent savedDirLight_{};

    bool hadPointLight_ = false;
    rendering::PointLightComponent savedPointLight_{};
};

}  // namespace engine::editor
