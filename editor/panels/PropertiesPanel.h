#pragma once

#include <cstdint>
#include <memory>

#include "editor/panels/IEditorPanel.h"
#include "engine/memory/InlinedVector.h"

// Forward declarations.
namespace engine::ecs
{
class Registry;
}

namespace engine::editor
{

class EditorState;
class IComponentInspector;
class IEditorWindow;

// ---------------------------------------------------------------------------
// PropertiesPanel -- shows and edits components of the selected entity.
//
// Uses registered IComponentInspector instances to render per-component
// editing UI via bgfx debug text.
// ---------------------------------------------------------------------------

class PropertiesPanel final : public IEditorPanel
{
public:
    PropertiesPanel(ecs::Registry& registry, EditorState& state, const IEditorWindow& window);
    ~PropertiesPanel() override;

    const char* panelName() const override
    {
        return "Properties";
    }

    void init() override;
    void shutdown() override;
    void update(float dt) override;
    void render() override;

    // Register a component inspector.  Ownership is transferred.
    void addInspector(std::unique_ptr<IComponentInspector> inspector);

private:
    ecs::Registry& registry_;
    EditorState& state_;
    const IEditorWindow& window_;

    memory::InlinedVector<std::unique_ptr<IComponentInspector>, 8> inspectors_;
};

}  // namespace engine::editor
