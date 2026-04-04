#pragma once

#include "editor/panels/IComponentInspector.h"

// Forward declarations.
namespace engine::editor
{
class IEditorWindow;
}

namespace engine::rendering
{
class RenderResources;
}

namespace engine::editor
{

// ---------------------------------------------------------------------------
// MaterialInspector -- inspects and edits material properties via the
// MaterialComponent + RenderResources material table.
//
// Displays albedo color (RGB), roughness, and metallic as editable fields.
// ---------------------------------------------------------------------------

class MaterialInspector final : public IComponentInspector
{
public:
    MaterialInspector(const IEditorWindow& window, rendering::RenderResources& resources);
    ~MaterialInspector() override = default;

    const char* componentName() const override
    {
        return "Material";
    }

    bool canInspect(const ecs::Registry& reg, ecs::EntityID entity) const override;

    uint16_t inspect(ecs::Registry& reg, ecs::EntityID entity, uint16_t startRow) override;

private:
    const IEditorWindow& window_;
    rendering::RenderResources& resources_;
};

}  // namespace engine::editor
