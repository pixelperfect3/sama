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
// NameInspector -- inspects and edits the NameComponent.
//
// Displays the entity name. Future phases will support text editing via
// native UI; for now the name is displayed read-only in debug text.
// ---------------------------------------------------------------------------

class NameInspector final : public IComponentInspector
{
public:
    explicit NameInspector(const IEditorWindow& window);
    ~NameInspector() override = default;

    const char* componentName() const override
    {
        return "Name";
    }

    bool canInspect(const ecs::Registry& reg, ecs::EntityID entity) const override;

    uint16_t inspect(ecs::Registry& reg, ecs::EntityID entity, uint16_t startRow) override;

private:
    const IEditorWindow& window_;
};

}  // namespace engine::editor
