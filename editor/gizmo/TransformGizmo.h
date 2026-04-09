#pragma once

#include <cstdint>
#include <glm/glm.hpp>

#include "engine/ecs/Entity.h"
#include "engine/rendering/EcsComponents.h"

// Forward declarations.
namespace engine::ecs
{
class Registry;
}

namespace engine::editor
{

class EditorState;
class IEditorWindow;

// ---------------------------------------------------------------------------
// GizmoMode -- translate / rotate / scale.
// ---------------------------------------------------------------------------

enum class GizmoMode : uint8_t
{
    Translate,
    Rotate,
    Scale,
};

// ---------------------------------------------------------------------------
// GizmoAxis -- which axis is hovered or being dragged.
// ---------------------------------------------------------------------------

enum class GizmoAxis : uint8_t
{
    None,
    X,
    Y,
    Z,
};

// ---------------------------------------------------------------------------
// TransformGizmo -- 3D transform manipulator for selected entities.
//
// Handles mouse interaction (hover detection, axis dragging) and applies
// transform changes to the ECS component.  Rendering is delegated to
// GizmoRenderer.
// ---------------------------------------------------------------------------

class TransformGizmo
{
public:
    TransformGizmo(ecs::Registry& registry, EditorState& state, const IEditorWindow& window);
    ~TransformGizmo() = default;

    void update(float dt, const glm::mat4& view, const glm::mat4& proj);

    [[nodiscard]] GizmoMode mode() const
    {
        return mode_;
    }
    [[nodiscard]] GizmoAxis hoveredAxis() const
    {
        return hoveredAxis_;
    }
    [[nodiscard]] GizmoAxis activeAxis() const
    {
        return activeAxis_;
    }
    [[nodiscard]] bool isDragging() const
    {
        return dragging_;
    }

    // Returns true the frame a drag just completed (for undo command creation).
    [[nodiscard]] bool dragJustEnded() const
    {
        return dragJustEnded_;
    }

    // The transform captured at the start of the most recent drag.
    [[nodiscard]] const rendering::TransformComponent& dragStartTransform() const
    {
        return dragStartTransform_;
    }

    // Returns the world position of the gizmo (selected entity's position).
    [[nodiscard]] glm::vec3 gizmoPosition() const
    {
        return gizmoPos_;
    }

private:
    // Compute a world-space ray from screen mouse position.
    void screenToRay(const glm::mat4& invVP, glm::vec3& origin, glm::vec3& direction) const;

    // Test ray against a cylinder-like axis and return distance (or -1).
    float rayAxisTest(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                      const glm::vec3& axisOrigin, const glm::vec3& axisDir, float axisLength,
                      float radius) const;

    // Project mouse movement onto an axis to get a world-space delta.
    float projectMouseOntoAxis(const glm::vec3& axis, const glm::mat4& view,
                               const glm::mat4& proj) const;

    ecs::Registry& registry_;
    EditorState& state_;
    const IEditorWindow& window_;

    GizmoMode mode_ = GizmoMode::Translate;
    GizmoAxis hoveredAxis_ = GizmoAxis::None;
    GizmoAxis activeAxis_ = GizmoAxis::None;
    bool dragging_ = false;
    bool wasLeftDown_ = false;

    glm::vec3 gizmoPos_{0.0f};
    glm::vec3 dragStartPos_{0.0f};
    bool dragJustEnded_ = false;
    rendering::TransformComponent dragStartTransform_{};

    // Rotation drag state: angle at drag start so we can compute deltas.
    float dragStartAngle_ = 0.0f;
    float dragPrevAngle_ = 0.0f;

    // Compute angle of mouse around the rotation axis (for rotate gizmo).
    float computeRotationAngle(const glm::vec3& axis, const glm::mat4& view,
                               const glm::mat4& proj) const;

    // Cached for drag computations.
    glm::mat4 cachedView_{1.0f};
    glm::mat4 cachedProj_{1.0f};
};

}  // namespace engine::editor
