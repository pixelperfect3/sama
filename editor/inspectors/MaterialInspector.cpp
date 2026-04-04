#include "editor/inspectors/MaterialInspector.h"

#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>

#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/Material.h"
#include "engine/rendering/RenderResources.h"

using namespace engine::ecs;
using namespace engine::rendering;

namespace engine::editor
{

MaterialInspector::MaterialInspector(const IEditorWindow& window, RenderResources& resources)
    : window_(window), resources_(resources)
{
}

bool MaterialInspector::canInspect(const Registry& reg, EntityID entity) const
{
    return reg.has<MaterialComponent>(entity);
}

uint16_t MaterialInspector::inspect(Registry& reg, EntityID entity, uint16_t startRow)
{
    auto* mc = reg.get<MaterialComponent>(entity);
    if (!mc)
        return 0;

    Material* mat = resources_.getMaterialMut(mc->material);
    if (!mat)
        return 0;

    constexpr uint16_t kCol = 55;
    uint16_t row = startRow;

    bgfx::dbgTextPrintf(kCol, row++, 0x0f, "--- Material ---");

    // Albedo color.
    bgfx::dbgTextPrintf(kCol, row++, 0x07, "Albedo R:% 5.2f  G:% 5.2f  B:% 5.2f", mat->albedo.x,
                        mat->albedo.y, mat->albedo.z);

    // Roughness.
    // Display as a simple bar: [====------] 0.40
    int roughBar = static_cast<int>(mat->roughness * 10.0f);
    char roughBuf[16];
    for (int i = 0; i < 10; ++i)
    {
        roughBuf[i] = (i < roughBar) ? '=' : '-';
    }
    roughBuf[10] = '\0';
    bgfx::dbgTextPrintf(kCol, row++, 0x07, "Rough  [%s] %.2f", roughBuf, mat->roughness);

    // Metallic.
    int metalBar = static_cast<int>(mat->metallic * 10.0f);
    char metalBuf[16];
    for (int i = 0; i < 10; ++i)
    {
        metalBuf[i] = (i < metalBar) ? '=' : '-';
    }
    metalBuf[10] = '\0';
    bgfx::dbgTextPrintf(kCol, row++, 0x07, "Metal  [%s] %.2f", metalBuf, mat->metallic);

    // Emissive scale.
    bgfx::dbgTextPrintf(kCol, row++, 0x07, "Emiss  %.2f", mat->emissiveScale);

    row++;  // blank line
    return static_cast<uint16_t>(row - startRow);
}

}  // namespace engine::editor
