#include "editor/panels/HierarchyPanel.h"

#include <bgfx/bgfx.h>

#include <cstdio>

#include "editor/EditorState.h"
#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/scene/HierarchyComponents.h"
#include "engine/scene/NameComponent.h"
#include "engine/scene/SceneGraph.h"

using namespace engine::ecs;
using namespace engine::rendering;
using namespace engine::scene;

namespace engine::editor
{

HierarchyPanel::HierarchyPanel(Registry& registry, EditorState& state, const IEditorWindow& window)
    : registry_(registry), state_(state), window_(window)
{
}

void HierarchyPanel::init() {}

void HierarchyPanel::shutdown() {}

void HierarchyPanel::update(float /*dt*/)
{
    if (!isVisible())
        return;

    // Click detection: if left mouse is pressed, map mouse Y to an entity row.
    if (window_.isLeftMouseDown())
    {
        double mx = window_.mouseX();
        double my = window_.mouseY();

        // bgfx debug text is 8x16 pixels per character cell.
        // Only handle clicks in the hierarchy column (x < 30 chars = 240 px).
        float scale = window_.contentScale();
        double charW = 8.0;
        double charH = 16.0;

        uint16_t col = static_cast<uint16_t>(mx / charW);
        uint16_t row = static_cast<uint16_t>(my / charH);

        if (col < 30 && row >= kStartY)
        {
            uint16_t entityRow = row - kStartY;

            // Walk entities to find which one is at that row.
            uint16_t currentRow = 0;
            EntityID clickedEntity = INVALID_ENTITY;

            registry_.forEachEntity(
                [&](EntityID e)
                {
                    if (clickedEntity != INVALID_ENTITY)
                        return;
                    if (currentRow == entityRow)
                    {
                        clickedEntity = e;
                    }
                    ++currentRow;
                });

            if (clickedEntity != INVALID_ENTITY)
            {
                state_.select(clickedEntity);
            }
        }
    }
}

void HierarchyPanel::render()
{
    if (!isVisible())
        return;

    // Header.
    bgfx::dbgTextPrintf(kStartX, kStartY - 1, 0x0f, "--- Scene Hierarchy ---");

    uint16_t row = kStartY;

    registry_.forEachEntity(
        [&](EntityID e)
        {
            if (row >= kStartY + kMaxRows)
                return;

            // Build display name.
            char label[64];
            const auto* name = registry_.get<NameComponent>(e);
            if (name && !name->name.empty())
            {
                snprintf(label, sizeof(label), "%s", name->name.c_str());
            }
            else
            {
                snprintf(label, sizeof(label), "Entity #%u", entityIndex(e));
            }

            // Build component tags string.
            char tags[64] = {};
            int tagLen = 0;
            if (registry_.has<TransformComponent>(e))
            {
                tagLen += snprintf(tags + tagLen, sizeof(tags) - tagLen, "[T]");
            }
            if (registry_.has<MeshComponent>(e))
            {
                tagLen += snprintf(tags + tagLen, sizeof(tags) - tagLen, "[M]");
            }
            if (registry_.has<MaterialComponent>(e))
            {
                tagLen += snprintf(tags + tagLen, sizeof(tags) - tagLen, "[Mat]");
            }
            if (registry_.has<DirectionalLightComponent>(e))
            {
                tagLen += snprintf(tags + tagLen, sizeof(tags) - tagLen, "[DL]");
            }
            if (registry_.has<PointLightComponent>(e))
            {
                tagLen += snprintf(tags + tagLen, sizeof(tags) - tagLen, "[PL]");
            }
            if (registry_.has<CameraComponent>(e))
            {
                tagLen += snprintf(tags + tagLen, sizeof(tags) - tagLen, "[Cam]");
            }

            // Highlight selected entity.
            bool selected = state_.isSelected(e);
            uint8_t color = selected ? 0x2f : 0x07;  // green bg / grey

            bgfx::dbgTextPrintf(kStartX, row, color, "  %-20s %s", label, tags);

            ++row;
        });
}

}  // namespace engine::editor
