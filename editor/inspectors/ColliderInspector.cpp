#include "editor/inspectors/ColliderInspector.h"

#include <bgfx/bgfx.h>

#include <cmath>
#include <cstdio>

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

static constexpr int kNumFields = 9;

ColliderInspector::ColliderInspector(const IEditorWindow& window) : window_(window) {}

bool ColliderInspector::canInspect(const Registry& reg, EntityID entity) const
{
    return reg.has<ColliderComponent>(entity);
}

static const char* shapeLabel(ColliderShape s)
{
    switch (s)
    {
        case ColliderShape::Box:
            return "Box";
        case ColliderShape::Sphere:
            return "Sphere";
        case ColliderShape::Capsule:
            return "Capsule";
        case ColliderShape::Mesh:
            return "Mesh";
        case ColliderShape::Compound:
            return "Compound";
    }
    return "?";
}

uint16_t ColliderInspector::inspect(Registry& reg, EntityID entity, uint16_t startRow)
{
    auto* cc = reg.get<ColliderComponent>(entity);
    if (!cc)
        return 0;

    constexpr uint16_t kCol = 55;
    uint16_t row = startRow;

    bgfx::dbgTextPrintf(kCol, row++, 0x0f, "--- Collider ---");

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

    const bool dec = window_.isKeyPressed(kKeyLeft) || window_.isKeyPressed(kKeyMinus);
    const bool inc = window_.isKeyPressed(kKeyRight) || window_.isKeyPressed(kKeyPlus);

    if (dec || inc)
    {
        const int dir = inc ? 1 : -1;
        switch (activeField_)
        {
            case 0:
            {
                // Cycle shape.
                int s = static_cast<int>(cc->shape) + dir;
                if (s < 0)
                    s = 3;
                if (s > 3)
                    s = 0;
                cc->shape = static_cast<ColliderShape>(s);
                break;
            }
            case 1:
                cc->offset.x += 0.1f * dir;
                break;
            case 2:
                cc->offset.y += 0.1f * dir;
                break;
            case 3:
                cc->offset.z += 0.1f * dir;
                break;
            case 4:
                cc->halfExtents.x = std::max(0.0f, cc->halfExtents.x + 0.1f * dir);
                break;
            case 5:
                cc->halfExtents.y = std::max(0.0f, cc->halfExtents.y + 0.1f * dir);
                break;
            case 6:
                cc->halfExtents.z = std::max(0.0f, cc->halfExtents.z + 0.1f * dir);
                break;
            case 7:
                cc->radius = std::max(0.0f, cc->radius + 0.1f * dir);
                break;
            case 8:
                cc->isSensor = cc->isSensor ? 0 : 1;
                break;
            default:
                break;
        }
    }

    auto line = [&](int field, const char* fmt, auto... args)
    {
        const uint8_t color = (field == activeField_) ? 0x1f : 0x07;
        bgfx::dbgTextPrintf(kCol, row++, color, fmt, args...);
    };

    line(0, "Shape       %s", shapeLabel(cc->shape));
    line(1, "Offset X  % 8.3f", cc->offset.x);
    line(2, "Offset Y  % 8.3f", cc->offset.y);
    line(3, "Offset Z  % 8.3f", cc->offset.z);
    line(4, "HalfExt X % 8.3f  (Box)", cc->halfExtents.x);
    line(5, "HalfExt Y % 8.3f  (Box)", cc->halfExtents.y);
    line(6, "HalfExt Z % 8.3f  (Box)", cc->halfExtents.z);
    line(7, "Radius    % 8.3f  (Sph/Cap)", cc->radius);
    line(8, "Sensor    %s", cc->isSensor ? "yes" : "no");

    row++;  // blank line
    return static_cast<uint16_t>(row - startRow);
}

}  // namespace engine::editor
