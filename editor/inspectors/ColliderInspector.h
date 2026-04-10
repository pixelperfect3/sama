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
// ColliderInspector -- inspects and edits ColliderComponent fields.
//
// Displays collider shape (Box/Sphere/Capsule/Mesh), offset, half extents
// (Box only), radius (Sphere/Capsule), and sensor flag.
// ---------------------------------------------------------------------------

class ColliderInspector final : public IComponentInspector
{
public:
    explicit ColliderInspector(const IEditorWindow& window);
    ~ColliderInspector() override = default;

    const char* componentName() const override
    {
        return "Collider";
    }

    bool canInspect(const ecs::Registry& reg, ecs::EntityID entity) const override;

    uint16_t inspect(ecs::Registry& reg, ecs::EntityID entity, uint16_t startRow) override;

private:
    const IEditorWindow& window_;
    // Fields: 0=shape, 1..3=offset xyz, 4..6=halfExtents xyz, 7=radius, 8=isSensor
    int activeField_ = 0;
};

}  // namespace engine::editor
