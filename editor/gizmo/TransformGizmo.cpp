#include "editor/gizmo/TransformGizmo.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "editor/EditorState.h"
#include "editor/platform/IEditorWindow.h"
#include "engine/ecs/Registry.h"
#include "engine/rendering/EcsComponents.h"

using namespace engine::ecs;
using namespace engine::rendering;

namespace engine::editor
{

// Gizmo axis length in world units (scaled by distance later).
static constexpr float kAxisLength = 1.0f;
static constexpr float kAxisRadius = 0.05f;
static constexpr float kHitRadius = 0.08f;

TransformGizmo::TransformGizmo(Registry& registry, EditorState& state, const IEditorWindow& window)
    : registry_(registry), state_(state), window_(window)
{
}

void TransformGizmo::screenToRay(const glm::mat4& invVP, glm::vec3& origin,
                                 glm::vec3& direction) const
{
    // Mouse is in logical (points); convert to framebuffer pixels since
    // bgfx renders at framebuffer resolution.
    float scale = window_.contentScale();
    float mx = static_cast<float>(window_.mouseX()) * scale;
    float my = static_cast<float>(window_.mouseY()) * scale;
    float w = static_cast<float>(window_.framebufferWidth());
    float h = static_cast<float>(window_.framebufferHeight());

    // Convert to NDC [-1, 1].
    float ndcX = (2.0f * mx / w) - 1.0f;
    float ndcY = 1.0f - (2.0f * my / h);

    glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPt = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

    nearPt /= nearPt.w;
    farPt /= farPt.w;

    origin = glm::vec3(nearPt);
    direction = glm::normalize(glm::vec3(farPt - nearPt));
}

float TransformGizmo::rayAxisTest(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                  const glm::vec3& axisOrigin, const glm::vec3& axisDir,
                                  float axisLength, float radius) const
{
    // Find the closest point between the ray and the axis line segment.
    glm::vec3 w = rayOrigin - axisOrigin;
    float a = glm::dot(rayDir, rayDir);
    float b = glm::dot(rayDir, axisDir);
    float c = glm::dot(axisDir, axisDir);
    float d = glm::dot(rayDir, w);
    float e = glm::dot(axisDir, w);

    float denom = a * c - b * b;
    if (std::abs(denom) < 1e-6f)
        return -1.0f;

    float s = (b * e - c * d) / denom;
    float t = (a * e - b * d) / denom;

    // Clamp t to the axis segment [0, axisLength].
    t = glm::clamp(t, 0.0f, axisLength);

    // Recompute s for clamped t.
    s = (glm::dot(axisDir, rayOrigin + rayDir * s - axisOrigin) > 0.0f) ? s : 0.0f;

    glm::vec3 closestOnRay = rayOrigin + rayDir * s;
    glm::vec3 closestOnAxis = axisOrigin + axisDir * t;
    float dist = glm::length(closestOnRay - closestOnAxis);

    if (dist < radius && s > 0.0f)
        return s;
    return -1.0f;
}

float TransformGizmo::projectMouseOntoAxis(const glm::vec3& axis, const glm::mat4& view,
                                           const glm::mat4& proj) const
{
    float dx = static_cast<float>(window_.mouseDeltaX());
    float dy = static_cast<float>(window_.mouseDeltaY());
    float w = static_cast<float>(window_.width());
    float h = static_cast<float>(window_.height());

    // Project the world axis to screen space to figure out the screen-space
    // direction of the axis.
    glm::mat4 vp = proj * view;
    glm::vec4 p0 = vp * glm::vec4(gizmoPos_, 1.0f);
    glm::vec4 p1 = vp * glm::vec4(gizmoPos_ + axis, 1.0f);

    if (std::abs(p0.w) < 1e-6f || std::abs(p1.w) < 1e-6f)
        return 0.0f;

    glm::vec2 s0 = glm::vec2(p0) / p0.w;
    glm::vec2 s1 = glm::vec2(p1) / p1.w;

    glm::vec2 screenAxis = s1 - s0;
    float screenAxisLen = glm::length(screenAxis);
    if (screenAxisLen < 1e-6f)
        return 0.0f;

    screenAxis /= screenAxisLen;

    // Mouse delta in NDC.
    glm::vec2 mouseDelta(2.0f * dx / w, -2.0f * dy / h);

    // Project mouse delta onto screen axis direction.
    float projected = glm::dot(mouseDelta, screenAxis);

    // Scale factor: how many world units per NDC unit along this axis.
    float worldPerNdc = 1.0f / screenAxisLen;

    return projected * worldPerNdc;
}

float TransformGizmo::computeRotationAngle(const glm::vec3& axis, const glm::mat4& view,
                                           const glm::mat4& proj) const
{
    // Project gizmo center to screen space.
    float scale = window_.contentScale();
    float mx = static_cast<float>(window_.mouseX()) * scale;
    float my = static_cast<float>(window_.mouseY()) * scale;
    float w = static_cast<float>(window_.framebufferWidth());
    float h = static_cast<float>(window_.framebufferHeight());

    glm::mat4 vp = proj * view;
    glm::vec4 center4 = vp * glm::vec4(gizmoPos_, 1.0f);
    if (std::abs(center4.w) < 1e-6f)
        return 0.0f;

    // Center in pixel coords.
    glm::vec2 centerScreen;
    centerScreen.x = (center4.x / center4.w * 0.5f + 0.5f) * w;
    centerScreen.y = (1.0f - (center4.y / center4.w * 0.5f + 0.5f)) * h;

    // Mouse position relative to center.
    float dx = mx - centerScreen.x;
    float dy = my - centerScreen.y;

    return std::atan2(dy, dx);
}

void TransformGizmo::update(float /*dt*/, const glm::mat4& view, const glm::mat4& proj)
{
    cachedView_ = view;
    cachedProj_ = proj;
    dragJustEnded_ = false;

    // Handle mode switching via keyboard.
    if (window_.isKeyPressed('W'))
        mode_ = GizmoMode::Translate;
    if (window_.isKeyPressed('E'))
        mode_ = GizmoMode::Rotate;
    if (window_.isKeyPressed('R'))
        mode_ = GizmoMode::Scale;

    // Gizmo writes to TransformComponent, which would fight
    // PhysicsSystem::syncDynamicBodies while the simulation is running.
    // Disable interaction outside of Editing mode.
    if (state_.playState() != EditorPlayState::Editing)
    {
        hoveredAxis_ = GizmoAxis::None;
        activeAxis_ = GizmoAxis::None;
        dragging_ = false;
        wasLeftDown_ = window_.isLeftMouseDown();
        return;
    }

    EntityID selE = state_.primarySelection();
    if (selE == INVALID_ENTITY)
    {
        hoveredAxis_ = GizmoAxis::None;
        activeAxis_ = GizmoAxis::None;
        dragging_ = false;
        wasLeftDown_ = window_.isLeftMouseDown();
        return;
    }

    // Get the selected entity's world position.
    auto* wt = registry_.get<WorldTransformComponent>(selE);
    if (!wt)
    {
        wasLeftDown_ = window_.isLeftMouseDown();
        return;
    }
    gizmoPos_ = glm::vec3(wt->matrix[3]);

    // Scale gizmo by distance to camera for constant screen size.
    glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
    float dist = glm::length(gizmoPos_ - camPos);
    float gizmoScale = dist * 0.15f;

    float scaledLength = kAxisLength * gizmoScale;
    float scaledHitRadius = kHitRadius * gizmoScale;

    // Cast a ray from the mouse.
    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec3 rayO, rayD;
    screenToRay(invVP, rayO, rayD);

    // Axis directions.
    const glm::vec3 axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    bool leftDown = window_.isLeftMouseDown();
    bool leftPressed = leftDown && !wasLeftDown_;
    bool leftReleased = !leftDown && wasLeftDown_;

    if (!dragging_)
    {
        // Hover test: find closest axis.
        hoveredAxis_ = GizmoAxis::None;
        float bestT = 1e30f;

        if (mode_ == GizmoMode::Rotate)
        {
            // For rotation, test ray against a circle (ring) in each axis plane.
            // The circle lies in the plane perpendicular to the axis, centered
            // at gizmoPos_, with radius = scaledLength * 0.8 (matches renderer).
            float ringRadius = scaledLength * 0.8f;
            float ringTolerance = scaledHitRadius * 2.0f;  // generous hit zone

            for (int i = 0; i < 3; ++i)
            {
                // Intersect ray with the plane perpendicular to axes[i]
                // passing through gizmoPos_.
                float denom = glm::dot(rayD, axes[i]);
                if (std::abs(denom) < 1e-6f)
                    continue;  // ray parallel to plane

                float tPlane = glm::dot(gizmoPos_ - rayO, axes[i]) / denom;
                if (tPlane < 0.0f)
                    continue;  // behind camera

                glm::vec3 hitPoint = rayO + rayD * tPlane;
                float distFromCenter = glm::length(hitPoint - gizmoPos_);

                // Check if the hit is near the ring (within tolerance).
                if (std::abs(distFromCenter - ringRadius) < ringTolerance && tPlane < bestT)
                {
                    bestT = tPlane;
                    hoveredAxis_ = static_cast<GizmoAxis>(i + 1);
                }
            }
        }
        else
        {
            // Translate/Scale: test ray against straight line axes.
            for (int i = 0; i < 3; ++i)
            {
                float t =
                    rayAxisTest(rayO, rayD, gizmoPos_, axes[i], scaledLength, scaledHitRadius);
                if (t > 0.0f && t < bestT)
                {
                    bestT = t;
                    hoveredAxis_ = static_cast<GizmoAxis>(i + 1);
                }
            }
        }

        // Start drag on left click.
        if (leftPressed && hoveredAxis_ != GizmoAxis::None)
        {
            dragging_ = true;
            activeAxis_ = hoveredAxis_;
            dragStartPos_ = gizmoPos_;

            // Capture the transform at drag start for undo.
            auto* tc = registry_.get<TransformComponent>(selE);
            if (tc)
            {
                dragStartTransform_ = *tc;
            }

            // For rotation mode, capture the starting angle.
            if (mode_ == GizmoMode::Rotate)
            {
                int axisIdx = static_cast<int>(hoveredAxis_) - 1;
                dragStartAngle_ = computeRotationAngle(axes[axisIdx], view, proj);
                dragPrevAngle_ = dragStartAngle_;
            }
        }
    }
    else
    {
        // Continue dragging.
        if (leftReleased)
        {
            dragging_ = false;
            activeAxis_ = GizmoAxis::None;
            dragJustEnded_ = true;
        }
        else
        {
            // Apply drag delta based on mode.
            int axisIdx = static_cast<int>(activeAxis_) - 1;
            glm::vec3 axisDir = axes[axisIdx];

            if (mode_ == GizmoMode::Translate)
            {
                float delta = projectMouseOntoAxis(axisDir * gizmoScale, view, proj);

                auto* tc = registry_.get<TransformComponent>(selE);
                if (tc)
                {
                    tc->position[axisIdx] += delta;
                    tc->flags |= 0x01;  // dirty
                }
            }
            else if (mode_ == GizmoMode::Scale)
            {
                float delta = projectMouseOntoAxis(axisDir * gizmoScale, view, proj);

                auto* tc = registry_.get<TransformComponent>(selE);
                if (tc)
                {
                    tc->scale[axisIdx] += delta;
                    tc->scale[axisIdx] = glm::max(tc->scale[axisIdx], 0.01f);
                    tc->flags |= 0x01;
                }
            }
            else if (mode_ == GizmoMode::Rotate)
            {
                float currentAngle = computeRotationAngle(axisDir, view, proj);
                float angleDelta = currentAngle - dragPrevAngle_;

                // Handle wrap-around at +/- PI.
                if (angleDelta > 3.14159265f)
                    angleDelta -= 2.0f * 3.14159265f;
                else if (angleDelta < -3.14159265f)
                    angleDelta += 2.0f * 3.14159265f;

                dragPrevAngle_ = currentAngle;

                auto* tc = registry_.get<TransformComponent>(selE);
                if (tc)
                {
                    glm::quat deltaRot = glm::angleAxis(angleDelta, axisDir);
                    tc->rotation = glm::normalize(deltaRot * glm::quat(tc->rotation));
                    tc->flags |= 0x01;  // dirty
                }
            }
        }
    }

    wasLeftDown_ = leftDown;
}

}  // namespace engine::editor
