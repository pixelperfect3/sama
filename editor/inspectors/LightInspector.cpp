#include "editor/inspectors/LightInspector.h"

#include <bgfx/bgfx.h>

#include <cstdio>

#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"

using namespace engine::ecs;
using namespace engine::rendering;

namespace engine::editor
{

LightInspector::LightInspector(const IEditorWindow& window) : window_(window) {}

bool LightInspector::canInspect(const Registry& reg, EntityID entity) const
{
    return reg.has<DirectionalLightComponent>(entity) || reg.has<PointLightComponent>(entity);
}

uint16_t LightInspector::inspect(Registry& reg, EntityID entity, uint16_t startRow)
{
    constexpr uint16_t kCol = 55;
    uint16_t row = startRow;

    // Directional light.
    auto* dl = reg.get<DirectionalLightComponent>(entity);
    if (dl)
    {
        bgfx::dbgTextPrintf(kCol, row++, 0x0f, "--- Directional Light ---");
        bgfx::dbgTextPrintf(kCol, row++, 0x07, "Dir  X:% 6.2f  Y:% 6.2f  Z:% 6.2f", dl->direction.x,
                            dl->direction.y, dl->direction.z);
        bgfx::dbgTextPrintf(kCol, row++, 0x07, "Col  R:% 5.2f  G:% 5.2f  B:% 5.2f", dl->color.x,
                            dl->color.y, dl->color.z);
        bgfx::dbgTextPrintf(kCol, row++, 0x07, "Intensity: %.2f", dl->intensity);
        bgfx::dbgTextPrintf(kCol, row++, 0x07, "Shadows: %s", (dl->flags & 1u) ? "yes" : "no");
        row++;
    }

    // Point light.
    auto* pl = reg.get<PointLightComponent>(entity);
    if (pl)
    {
        bgfx::dbgTextPrintf(kCol, row++, 0x0f, "--- Point Light ---");
        bgfx::dbgTextPrintf(kCol, row++, 0x07, "Col  R:% 5.2f  G:% 5.2f  B:% 5.2f", pl->color.x,
                            pl->color.y, pl->color.z);
        bgfx::dbgTextPrintf(kCol, row++, 0x07, "Intensity: %.2f", pl->intensity);
        bgfx::dbgTextPrintf(kCol, row++, 0x07, "Radius: %.2f", pl->radius);
        row++;
    }

    return static_cast<uint16_t>(row - startRow);
}

}  // namespace engine::editor
