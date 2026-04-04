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
// LightInspector -- inspects DirectionalLightComponent and PointLightComponent.
//
// Displays direction, color, intensity, and radius (for point lights).
// ---------------------------------------------------------------------------

class LightInspector final : public IComponentInspector
{
public:
    explicit LightInspector(const IEditorWindow& window);
    ~LightInspector() override = default;

    const char* componentName() const override
    {
        return "Light";
    }

    bool canInspect(const ecs::Registry& reg, ecs::EntityID entity) const override;

    uint16_t inspect(ecs::Registry& reg, ecs::EntityID entity, uint16_t startRow) override;

private:
    const IEditorWindow& window_;
};

}  // namespace engine::editor
