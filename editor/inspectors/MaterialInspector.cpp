#include "editor/inspectors/MaterialInspector.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "editor/EditorState.h"
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

// Key codes matching CocoaEditorWindow mapping (mirrors TransformInspector).
static constexpr uint8_t kKeyUp = 0x83;
static constexpr uint8_t kKeyDown = 0x82;
static constexpr uint8_t kKeyLeft = 0x80;
static constexpr uint8_t kKeyRight = 0x81;
static constexpr uint8_t kKeyTab = 0x09;
static constexpr uint8_t kKeyPlus = '=';
static constexpr uint8_t kKeyMinus = '-';

static constexpr int kNumFields = 6;  // albedo R/G/B, roughness, metallic, emissive

MaterialInspector::MaterialInspector(const IEditorWindow& window, RenderResources& resources)
    : window_(window), resources_(resources)
{
}

bool MaterialInspector::canInspect(const Registry& reg, EntityID entity) const
{
    return reg.has<MaterialComponent>(entity);
}

uint16_t MaterialInspector::inspect(Registry& reg, EntityID entity, const EditorState& state,
                                    uint16_t startRow)
{
    auto* mc = reg.get<MaterialComponent>(entity);
    if (!mc)
        return 0;

    // RenderResources owns the live material slot read each frame by
    // DrawCallBuildSystem; mutating it here propagates to the next draw with
    // no extra invalidation step.
    Material* mat = resources_.getMaterialMut(mc->material);
    if (!mat)
        return 0;

    constexpr uint16_t kCol = 55;
    uint16_t row = startRow;

    bgfx::dbgTextPrintf(kCol, row++, 0x0f, "--- Material ---");

    const bool editsAllowed = state.playState() == EditorPlayState::Editing;

    // --- Field navigation (always allowed; lets the user inspect at any time).
    if (window_.isKeyPressed(kKeyTab))
    {
        activeField_ = (activeField_ + 1) % kNumFields;
    }
    if (window_.isKeyPressed(kKeyUp))
    {
        activeField_ = (activeField_ + kNumFields - 1) % kNumFields;
    }
    if (window_.isKeyPressed(kKeyDown))
    {
        activeField_ = (activeField_ + 1) % kNumFields;
    }

    // --- Edit the active field via Left/Right or -/+ (gated on play state).
    const bool dec =
        editsAllowed && (window_.isKeyPressed(kKeyLeft) || window_.isKeyPressed(kKeyMinus));
    const bool inc =
        editsAllowed && (window_.isKeyPressed(kKeyRight) || window_.isKeyPressed(kKeyPlus));

    if (dec || inc)
    {
        const float dir = inc ? 1.0f : -1.0f;
        switch (activeField_)
        {
            case 0:
                mat->albedo.x = std::clamp(mat->albedo.x + 0.05f * dir, 0.0f, 1.0f);
                break;
            case 1:
                mat->albedo.y = std::clamp(mat->albedo.y + 0.05f * dir, 0.0f, 1.0f);
                break;
            case 2:
                mat->albedo.z = std::clamp(mat->albedo.z + 0.05f * dir, 0.0f, 1.0f);
                break;
            case 3:
                mat->roughness = std::clamp(mat->roughness + 0.05f * dir, 0.0f, 1.0f);
                break;
            case 4:
                mat->metallic = std::clamp(mat->metallic + 0.05f * dir, 0.0f, 1.0f);
                break;
            case 5:
                mat->emissiveScale = std::max(0.0f, mat->emissiveScale + 0.1f * dir);
                break;
            default:
                break;
        }
    }

    // --- Render. Active field is highlighted (0x1f = white-on-blue).
    auto line = [&](int field, const char* fmt, auto... args)
    {
        const uint8_t color = (field == activeField_) ? 0x1f : 0x07;
        bgfx::dbgTextPrintf(kCol, row++, color, fmt, args...);
    };

    line(0, "Albedo R % 5.2f", mat->albedo.x);
    line(1, "Albedo G % 5.2f", mat->albedo.y);
    line(2, "Albedo B % 5.2f", mat->albedo.z);

    // Roughness bar: [====------] 0.40
    {
        int roughBar = static_cast<int>(mat->roughness * 10.0f);
        char roughBuf[16];
        for (int i = 0; i < 10; ++i)
        {
            roughBuf[i] = (i < roughBar) ? '=' : '-';
        }
        roughBuf[10] = '\0';
        line(3, "Rough  [%s] %.2f", roughBuf, mat->roughness);
    }

    // Metallic bar.
    {
        int metalBar = static_cast<int>(mat->metallic * 10.0f);
        char metalBuf[16];
        for (int i = 0; i < 10; ++i)
        {
            metalBuf[i] = (i < metalBar) ? '=' : '-';
        }
        metalBuf[10] = '\0';
        line(4, "Metal  [%s] %.2f", metalBuf, mat->metallic);
    }

    line(5, "Emiss  %.2f", mat->emissiveScale);

    if (!editsAllowed)
    {
        bgfx::dbgTextPrintf(kCol, row++, 0x08, "(read-only while playing)");
    }

    row++;  // blank line
    return static_cast<uint16_t>(row - startRow);
}

}  // namespace engine::editor
