#include "editor/inspectors/RigidBodyInspector.h"

#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>

#include "editor/EditorState.h"
#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Entity.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsComponents.h"

using namespace engine::ecs;
using namespace engine::physics;

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

static constexpr int kNumFields = 7;

RigidBodyInspector::RigidBodyInspector(const IEditorWindow& window) : window_(window) {}

bool RigidBodyInspector::canInspect(const Registry& reg, EntityID entity) const
{
    return reg.has<RigidBodyComponent>(entity);
}

static const char* bodyTypeLabel(BodyType t)
{
    switch (t)
    {
        case BodyType::Static:
            return "Static";
        case BodyType::Dynamic:
            return "Dynamic";
        case BodyType::Kinematic:
            return "Kinematic";
    }
    return "?";
}

uint16_t RigidBodyInspector::inspect(Registry& reg, EntityID entity, const EditorState& state,
                                     uint16_t startRow)
{
    auto* rb = reg.get<RigidBodyComponent>(entity);
    if (!rb)
        return 0;

    constexpr uint16_t kCol = 55;
    uint16_t row = startRow;

    bgfx::dbgTextPrintf(kCol, row++, 0x0f, "--- RigidBody ---");

    // Mirrors TransformInspector's gate: live edits to mass/damping/etc.
    // would race PhysicsSystem during Play. Navigation is still allowed.
    const bool editsAllowed = state.playState() == EditorPlayState::Editing;

    // Field navigation (Tab / Up / Down).
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

    // Edit the active field via Left/Right or -/+.
    const bool dec =
        editsAllowed && (window_.isKeyPressed(kKeyLeft) || window_.isKeyPressed(kKeyMinus));
    const bool inc =
        editsAllowed && (window_.isKeyPressed(kKeyRight) || window_.isKeyPressed(kKeyPlus));

    if (dec || inc)
    {
        const int dir = inc ? 1 : -1;
        switch (activeField_)
        {
            case 0:
            {
                // Cycle body type.
                int t = static_cast<int>(rb->type) + dir;
                if (t < 0)
                    t = 2;
                if (t > 2)
                    t = 0;
                rb->type = static_cast<BodyType>(t);
                break;
            }
            case 1:
                rb->mass = std::max(0.0f, rb->mass + 0.1f * dir);
                break;
            case 2:
                rb->linearDamping = std::max(0.0f, rb->linearDamping + 0.01f * dir);
                break;
            case 3:
                rb->angularDamping = std::max(0.0f, rb->angularDamping + 0.01f * dir);
                break;
            case 4:
                rb->friction = std::max(0.0f, std::min(1.0f, rb->friction + 0.05f * dir));
                break;
            case 5:
                rb->restitution = std::max(0.0f, std::min(1.0f, rb->restitution + 0.05f * dir));
                break;
            case 6:
            {
                int l = static_cast<int>(rb->layer) + dir;
                if (l < 0)
                    l = 0;
                if (l > 31)
                    l = 31;
                rb->layer = static_cast<uint8_t>(l);
                break;
            }
            default:
                break;
        }
    }

    auto line = [&](int field, const char* fmt, auto... args)
    {
        const uint8_t color = (field == activeField_) ? 0x1f : 0x07;
        bgfx::dbgTextPrintf(kCol, row++, color, fmt, args...);
    };

    line(0, "Type        %s", bodyTypeLabel(rb->type));
    line(1, "Mass        % 8.3f", rb->mass);
    line(2, "LinDamp     % 8.3f", rb->linearDamping);
    line(3, "AngDamp     % 8.3f", rb->angularDamping);
    line(4, "Friction    % 8.3f", rb->friction);
    line(5, "Restitution % 8.3f", rb->restitution);
    line(6, "Layer       %u", static_cast<unsigned>(rb->layer));

    if (!editsAllowed)
    {
        bgfx::dbgTextPrintf(kCol, row++, 0x08, "(read-only while playing)");
    }

    row++;  // blank line
    return static_cast<uint16_t>(row - startRow);
}

}  // namespace engine::editor
