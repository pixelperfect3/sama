#include "editor/inspectors/TransformInspector.h"

#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "editor/EditorState.h"
#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"

using namespace engine::ecs;
using namespace engine::rendering;

namespace engine::editor
{

// Key codes matching CocoaEditorWindow mapping.
static constexpr uint8_t kKeyUp = 0x83;
static constexpr uint8_t kKeyDown = 0x82;
static constexpr uint8_t kKeyLeft = 0x80;
static constexpr uint8_t kKeyRight = 0x81;
static constexpr uint8_t kKeyTab = 0x09;
static constexpr uint8_t kKeyPlus = '=';
static constexpr uint8_t kKeyMinus = '-';

TransformInspector::TransformInspector(const IEditorWindow& window) : window_(window) {}

bool TransformInspector::canInspect(const Registry& reg, EntityID entity) const
{
    return reg.has<TransformComponent>(entity);
}

uint16_t TransformInspector::inspect(Registry& reg, EntityID entity, const EditorState& state,
                                     uint16_t startRow)
{
    auto* tc = reg.get<TransformComponent>(entity);
    if (!tc)
        return 0;

    // Column where the properties panel starts (right side of screen).
    constexpr uint16_t kCol = 55;
    uint16_t row = startRow;

    // Header.
    bgfx::dbgTextPrintf(kCol, row++, 0x0f, "--- Transform ---");

    // Convert rotation to euler angles for display.
    glm::vec3 euler = glm::degrees(glm::eulerAngles(tc->rotation));

    // Field values array for easy access.
    float values[9] = {
        tc->position.x, tc->position.y, tc->position.z, euler.x,     euler.y,
        euler.z,        tc->scale.x,    tc->scale.y,    tc->scale.z,
    };

    const char* labels[3] = {"Pos", "Rot", "Scl"};
    const char* axes[3] = {"X", "Y", "Z"};

    // Play-state gate: keyboard edits to TransformComponent would race
    // PhysicsSystem::syncDynamicBodies during simulation. The gizmo and the
    // hierarchy / properties callbacks already gate on `playState() != Editing`
    // (see TransformGizmo.cpp:170, EditorApp.cpp:1421); the inspector keyboard
    // path was the missing third entry-point. Field navigation is still
    // allowed (read-only inspection during Play is useful), but value edits
    // are suppressed.
    const bool editsAllowed = state.playState() == EditorPlayState::Editing;

    // Handle keyboard input for field navigation and value editing.
    bool changed = false;

    if (window_.isKeyPressed(kKeyTab))
    {
        activeField_ = (activeField_ + 1) % 9;
    }
    if (window_.isKeyPressed(kKeyUp))
    {
        activeField_ = (activeField_ + 9 - 1) % 9;
    }
    if (window_.isKeyPressed(kKeyDown))
    {
        activeField_ = (activeField_ + 1) % 9;
    }

    // Determine increment based on field type.
    float increment = 0.0f;
    if (editsAllowed && (window_.isKeyPressed(kKeyRight) || window_.isKeyPressed(kKeyPlus)))
    {
        if (activeField_ < 3)
            increment = 0.1f;  // position
        else if (activeField_ < 6)
            increment = 5.0f;  // rotation (degrees)
        else
            increment = 0.1f;  // scale
    }
    if (editsAllowed && (window_.isKeyPressed(kKeyLeft) || window_.isKeyPressed(kKeyMinus)))
    {
        if (activeField_ < 3)
            increment = -0.1f;
        else if (activeField_ < 6)
            increment = -5.0f;
        else
            increment = -0.1f;
    }

    if (std::abs(increment) > 0.001f)
    {
        values[activeField_] += increment;
        changed = true;
    }

    // Render fields.
    for (int group = 0; group < 3; ++group)
    {
        char buf[128];
        int fieldBase = group * 3;

        for (int axis = 0; axis < 3; ++axis)
        {
            int field = fieldBase + axis;
            bool active = (field == activeField_);
            uint8_t color = active ? 0x1f : 0x07;

            if (axis == 0)
            {
                snprintf(buf, sizeof(buf), "%s  %s:% 8.3f", labels[group], axes[axis],
                         values[field]);
            }
            else
            {
                snprintf(buf, sizeof(buf), "     %s:% 8.3f", axes[axis], values[field]);
            }

            bgfx::dbgTextPrintf(kCol, row++, color, "%s", buf);
        }
    }

    // Hint when edits are gated so the user understands the no-op.
    if (!editsAllowed)
    {
        bgfx::dbgTextPrintf(kCol, row++, 0x08, "(read-only while playing)");
    }

    // Apply changes back to the component.
    if (changed)
    {
        tc->position = {values[0], values[1], values[2]};

        glm::vec3 newEuler = {values[3], values[4], values[5]};
        tc->rotation = glm::quat(glm::radians(newEuler));

        tc->scale = {values[6], values[7], values[8]};

        // Mark transform as dirty.
        tc->flags |= 0x01;
    }

    row++;  // blank line
    return static_cast<uint16_t>(row - startRow);
}

}  // namespace engine::editor
