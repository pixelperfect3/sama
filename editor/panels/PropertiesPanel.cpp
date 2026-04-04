#include "editor/panels/PropertiesPanel.h"

#include <bgfx/bgfx.h>

#include <cstdio>

#include "editor/EditorState.h"
#include "editor/panels/IComponentInspector.h"
#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/scene/NameComponent.h"

using namespace engine::ecs;

namespace engine::editor
{

PropertiesPanel::PropertiesPanel(Registry& registry, EditorState& state,
                                 const IEditorWindow& window)
    : registry_(registry), state_(state), window_(window)
{
}

PropertiesPanel::~PropertiesPanel() = default;

void PropertiesPanel::init() {}

void PropertiesPanel::shutdown() {}

void PropertiesPanel::update(float /*dt*/)
{
    if (!isVisible())
        return;

    // Inspectors may handle input during render (inspect() call).
}

void PropertiesPanel::render()
{
    if (!isVisible())
        return;

    constexpr uint16_t kCol = 55;
    constexpr uint16_t kStartRow = 3;

    // Header.
    bgfx::dbgTextPrintf(kCol, kStartRow, 0x0f, "--- Properties ---");

    EntityID entity = state_.primarySelection();
    if (entity == INVALID_ENTITY)
    {
        bgfx::dbgTextPrintf(kCol, kStartRow + 1, 0x07, "No entity selected");
        return;
    }

    // Show entity name / ID.
    const auto* name = registry_.get<engine::scene::NameComponent>(entity);
    if (name && !name->name.empty())
    {
        bgfx::dbgTextPrintf(kCol, kStartRow + 1, 0x0e, "%s  (id:%u)", name->name.c_str(),
                            entityIndex(entity));
    }
    else
    {
        bgfx::dbgTextPrintf(kCol, kStartRow + 1, 0x0e, "Entity #%u", entityIndex(entity));
    }

    bgfx::dbgTextPrintf(kCol, kStartRow + 2, 0x07, "Tab/Arrows=navigate  +/-=edit");

    // Render each applicable inspector.
    uint16_t row = kStartRow + 4;
    for (auto& inspector : inspectors_)
    {
        if (inspector->canInspect(registry_, entity))
        {
            uint16_t consumed = inspector->inspect(registry_, entity, row);
            row += consumed;
        }
    }
}

void PropertiesPanel::addInspector(std::unique_ptr<IComponentInspector> inspector)
{
    inspectors_.push_back(std::move(inspector));
}

}  // namespace engine::editor
