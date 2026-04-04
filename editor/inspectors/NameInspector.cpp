#include "editor/inspectors/NameInspector.h"

#include <bgfx/bgfx.h>

#include <cstdio>

#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/scene/NameComponent.h"

using namespace engine::ecs;

namespace engine::editor
{

NameInspector::NameInspector(const IEditorWindow& window) : window_(window) {}

bool NameInspector::canInspect(const Registry& reg, EntityID entity) const
{
    return reg.has<engine::scene::NameComponent>(entity);
}

uint16_t NameInspector::inspect(Registry& reg, EntityID entity, uint16_t startRow)
{
    auto* nc = reg.get<engine::scene::NameComponent>(entity);
    if (!nc)
        return 0;

    constexpr uint16_t kCol = 55;
    uint16_t row = startRow;

    bgfx::dbgTextPrintf(kCol, row++, 0x0f, "--- Name ---");
    bgfx::dbgTextPrintf(kCol, row++, 0x07, "Name: %s", nc->name.c_str());
    row++;  // blank line

    return static_cast<uint16_t>(row - startRow);
}

}  // namespace engine::editor
