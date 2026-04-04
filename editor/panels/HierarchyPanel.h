#pragma once

#include <cstdint>

#include "editor/panels/IEditorPanel.h"

// Forward declarations — no heavy headers in the panel header.
namespace engine::ecs
{
class Registry;
using EntityID = uint64_t;
}  // namespace engine::ecs

namespace engine::editor
{

class EditorState;
class IEditorWindow;

// ---------------------------------------------------------------------------
// HierarchyPanel -- shows all entities in a flat/tree list using bgfx debug
// text.  Click detection maps mouse Y to an entity row for selection.
// ---------------------------------------------------------------------------

class HierarchyPanel final : public IEditorPanel
{
public:
    HierarchyPanel(ecs::Registry& registry, EditorState& state, const IEditorWindow& window);
    ~HierarchyPanel() override = default;

    const char* panelName() const override
    {
        return "Hierarchy";
    }

    void init() override;
    void shutdown() override;
    void update(float dt) override;
    void render() override;

private:
    ecs::Registry& registry_;
    EditorState& state_;
    const IEditorWindow& window_;

    // Layout constants (in debug-text character cells).
    static constexpr uint16_t kStartX = 1;
    static constexpr uint16_t kStartY = 4;
    static constexpr uint16_t kMaxRows = 40;
};

}  // namespace engine::editor
