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
// TransformInspector -- inspects and edits TransformComponent.
//
// Displays position (x,y,z), rotation (euler angles), and scale (x,y,z).
// Arrow keys or +/- keys increment/decrement the active field.
// ---------------------------------------------------------------------------

class TransformInspector final : public IComponentInspector
{
public:
    explicit TransformInspector(const IEditorWindow& window);
    ~TransformInspector() override = default;

    const char* componentName() const override
    {
        return "Transform";
    }

    bool canInspect(const ecs::Registry& reg, ecs::EntityID entity) const override;

    uint16_t inspect(ecs::Registry& reg, ecs::EntityID entity, const EditorState& state,
                     uint16_t startRow) override;

private:
    const IEditorWindow& window_;

    // Active field for keyboard editing: 0-2 = pos xyz, 3-5 = rot xyz, 6-8 = scale xyz.
    int activeField_ = 0;
};

}  // namespace engine::editor
