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
// Displays albedo color (RGB), roughness, metallic, and emissive scale as
// editable fields. Edits write through `RenderResources::getMaterialMut()` —
// the PBR draw-call builder reads the same `Material*` per frame
// (`DrawCallBuildSystem.cpp:99`), so no cache invalidation is needed.
//
// Edits are gated on `EditorState::playState() == Editing` for symmetry with
// the other physics-affecting inspectors; material edits don't actually
// race the simulation, but rejecting them in Play matches user expectation
// that "the world is read-only when running".
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

    uint16_t inspect(ecs::Registry& reg, ecs::EntityID entity, const EditorState& state,
                     uint16_t startRow) override;

private:
    const IEditorWindow& window_;
    rendering::RenderResources& resources_;

    // Active field for keyboard editing:
    //   0..2 = albedo R/G/B, 3 = roughness, 4 = metallic, 5 = emissive scale.
    int activeField_ = 0;
};

}  // namespace engine::editor
