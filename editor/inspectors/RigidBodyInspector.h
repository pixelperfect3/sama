#pragma once

#include "editor/panels/IComponentInspector.h"

// Forward declarations.
namespace engine::editor
{
class IEditorWindow;
}

namespace engine::editor
{

// ---------------------------------------------------------------------------
// RigidBodyInspector -- inspects and edits RigidBodyComponent fields.
//
// Displays body type (Static/Dynamic/Kinematic), mass, damping, friction,
// restitution, and collision layer. The Jolt-managed bodyID is not exposed.
// ---------------------------------------------------------------------------

class RigidBodyInspector final : public IComponentInspector
{
public:
    explicit RigidBodyInspector(const IEditorWindow& window);
    ~RigidBodyInspector() override = default;

    const char* componentName() const override
    {
        return "RigidBody";
    }

    bool canInspect(const ecs::Registry& reg, ecs::EntityID entity) const override;

    uint16_t inspect(ecs::Registry& reg, ecs::EntityID entity, uint16_t startRow) override;

private:
    const IEditorWindow& window_;
    int activeField_ =
        0;  // 0=type, 1=mass, 2=linDamp, 3=angDamp, 4=friction, 5=restitution, 6=layer
};

}  // namespace engine::editor
