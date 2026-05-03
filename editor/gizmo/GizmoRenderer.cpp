#include "editor/gizmo/GizmoRenderer.h"

#include <bgfx/bgfx.h>

#include <cmath>
#include <glm/gtc/type_ptr.hpp>

#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/ShaderLoader.h"

namespace engine::editor
{

// Must match TransformGizmo's axis length for consistent visuals.
static constexpr float kAxisLength = 1.0f;

GizmoRenderer::~GizmoRenderer()
{
    shutdown();
}

void GizmoRenderer::init()
{
    if (initialized_)
        return;

    // Vertex layout: position (float3) + color (uint8x4 normalized).
    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    // Use the gizmo shader program (position + vertex color passthrough).
    // ShaderLoader returns engine::rendering::ProgramHandle (bgfx-free
    // wrapper); widen to bgfx for the engine-internal storage member.
    program_ = bgfx::ProgramHandle{engine::rendering::loadGizmoProgram().idx};

    initialized_ = true;
}

void GizmoRenderer::shutdown()
{
    if (!initialized_)
        return;

    if (bgfx::isValid(program_))
    {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }

    initialized_ = false;
}

void GizmoRenderer::drawLine(const glm::vec3& from, const glm::vec3& to, uint32_t color)
{
    // Draw 3 parallel lines offset slightly in camera-perpendicular direction
    // to create a thicker appearance (~3px). Single lines are nearly invisible
    // on Retina displays.
    glm::vec3 lineDir = to - from;
    float lineLen = glm::length(lineDir);
    if (lineLen < 1e-6f)
        return;
    lineDir /= lineLen;

    glm::vec3 midpoint = (from + to) * 0.5f;
    glm::vec3 toCamera = glm::normalize(cameraPos_ - midpoint);
    glm::vec3 side = glm::normalize(glm::cross(lineDir, toCamera));

    // Offset scaled by distance for consistent screen-space thickness.
    float dist = glm::length(cameraPos_ - midpoint);
    float pixelOff = dist * 0.001f;  // ~1 pixel in world space

    // Center line + 2 offset lines = 6 vertices total.
    if (lineVertCount_ + 6 > kMaxLineVerts)
        return;

    // Center line.
    lineVerts_[lineVertCount_++] = {from.x, from.y, from.z, color};
    lineVerts_[lineVertCount_++] = {to.x, to.y, to.z, color};

    // Offset line +side.
    glm::vec3 off = side * pixelOff;
    lineVerts_[lineVertCount_++] = {from.x + off.x, from.y + off.y, from.z + off.z, color};
    lineVerts_[lineVertCount_++] = {to.x + off.x, to.y + off.y, to.z + off.z, color};

    // Offset line -side.
    lineVerts_[lineVertCount_++] = {from.x - off.x, from.y - off.y, from.z - off.z, color};
    lineVerts_[lineVertCount_++] = {to.x - off.x, to.y - off.y, to.z - off.z, color};
}

void GizmoRenderer::drawArrow(const glm::vec3& origin, const glm::vec3& dir, float length,
                              uint32_t color)
{
    glm::vec3 tip = origin + dir * length;

    // Main shaft.
    drawLine(origin, tip, color);

    // Arrowhead: four small lines forming a cone approximation.
    float headLen = length * 0.15f;
    float headRad = length * 0.06f;

    // Find two perpendicular vectors to dir.
    glm::vec3 perp1;
    if (std::abs(dir.y) < 0.99f)
    {
        perp1 = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
    }
    else
    {
        perp1 = glm::normalize(glm::cross(dir, glm::vec3(1, 0, 0)));
    }
    glm::vec3 perp2 = glm::cross(dir, perp1);

    glm::vec3 headBase = tip - dir * headLen;

    // Draw 4 lines from tip to base ring.
    for (int i = 0; i < 4; ++i)
    {
        float angle = static_cast<float>(i) * 3.14159265f * 0.5f;
        glm::vec3 offset = (perp1 * std::cos(angle) + perp2 * std::sin(angle)) * headRad;
        drawLine(tip, headBase + offset, color);
    }
}

void GizmoRenderer::flush(const glm::mat4& view, const glm::mat4& proj, uint16_t fbWidth,
                          uint16_t fbHeight)
{
    if (lineVertCount_ == 0)
        return;

    // Configure the gizmo view: overlay, no depth test against scene.
    bgfx::setViewName(kGizmoView, "Gizmo");
    bgfx::setViewRect(kGizmoView, 0, 0, fbWidth, fbHeight);
    bgfx::setViewClear(kGizmoView, BGFX_CLEAR_NONE);
    bgfx::setViewTransform(kGizmoView, glm::value_ptr(view), glm::value_ptr(proj));

    // Allocate transient vertex buffer.
    bgfx::TransientVertexBuffer tvb;
    if (!bgfx::getAvailTransientVertexBuffer(lineVertCount_, layout_))
    {
        lineVertCount_ = 0;
        return;
    }
    bgfx::allocTransientVertexBuffer(&tvb, lineVertCount_, layout_);
    memcpy(tvb.data, lineVerts_, lineVertCount_ * sizeof(LineVertex));

    bgfx::setVertexBuffer(0, &tvb);

    // Identity transform.
    glm::mat4 identity(1.0f);
    bgfx::setTransform(glm::value_ptr(identity));

    // Render as lines with anti-aliasing.
    uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_PT_LINES | BGFX_STATE_LINEAA;

    bgfx::setState(state);
    bgfx::submit(kGizmoView, program_);

    lineVertCount_ = 0;
}

void GizmoRenderer::render(const TransformGizmo& gizmo, const glm::mat4& view,
                           const glm::mat4& proj, uint16_t fbWidth, uint16_t fbHeight)
{
    if (!initialized_)
        return;

    glm::vec3 pos = gizmo.gizmoPosition();

    // Scale gizmo by distance for constant screen size.
    glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
    cameraPos_ = camPos;
    float dist = glm::length(pos - camPos);
    float scale = dist * 0.15f;

    // Axis colors (ABGR format for bgfx).
    // Normal colors.
    uint32_t xColor = 0xFF0000FF;  // Red
    uint32_t yColor = 0xFF00FF00;  // Green
    uint32_t zColor = 0xFFFF0000;  // Blue

    // Brighten hovered/active axis.
    GizmoAxis hovered = gizmo.hoveredAxis();
    GizmoAxis active = gizmo.activeAxis();

    if (hovered == GizmoAxis::X || active == GizmoAxis::X)
        xColor = 0xFF4444FF;
    if (hovered == GizmoAxis::Y || active == GizmoAxis::Y)
        yColor = 0xFF44FF44;
    if (hovered == GizmoAxis::Z || active == GizmoAxis::Z)
        zColor = 0xFFFF4444;

    float len = kAxisLength * scale;

    if (gizmo.mode() == GizmoMode::Translate)
    {
        // Draw arrows for translate mode.
        drawArrow(pos, {1, 0, 0}, len, xColor);
        drawArrow(pos, {0, 1, 0}, len, yColor);
        drawArrow(pos, {0, 0, 1}, len, zColor);
    }
    else if (gizmo.mode() == GizmoMode::Scale)
    {
        // Draw lines with small cubes at ends (simplified as crosses).
        float cubeSize = len * 0.08f;

        auto drawScaleAxis = [&](const glm::vec3& dir, uint32_t color)
        {
            glm::vec3 tip = pos + dir * len;
            drawLine(pos, tip, color);

            // Small cross at tip.
            glm::vec3 perp1, perp2;
            if (std::abs(dir.y) < 0.99f)
            {
                perp1 = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
            }
            else
            {
                perp1 = glm::normalize(glm::cross(dir, glm::vec3(1, 0, 0)));
            }
            perp2 = glm::cross(dir, perp1);

            drawLine(tip - perp1 * cubeSize, tip + perp1 * cubeSize, color);
            drawLine(tip - perp2 * cubeSize, tip + perp2 * cubeSize, color);
        };

        drawScaleAxis({1, 0, 0}, xColor);
        drawScaleAxis({0, 1, 0}, yColor);
        drawScaleAxis({0, 0, 1}, zColor);
    }
    else
    {
        // Rotate mode: draw circles (simplified as line segments).
        constexpr int kSegments = 32;
        float r = len * 0.8f;

        auto drawCircle = [&](const glm::vec3& axis1, const glm::vec3& axis2, uint32_t color)
        {
            for (int i = 0; i < kSegments; ++i)
            {
                float a0 =
                    static_cast<float>(i) * 2.0f * 3.14159265f / static_cast<float>(kSegments);
                float a1 =
                    static_cast<float>(i + 1) * 2.0f * 3.14159265f / static_cast<float>(kSegments);
                glm::vec3 p0 = pos + (axis1 * std::cos(a0) + axis2 * std::sin(a0)) * r;
                glm::vec3 p1 = pos + (axis1 * std::cos(a1) + axis2 * std::sin(a1)) * r;
                drawLine(p0, p1, color);
            }
        };

        drawCircle({0, 1, 0}, {0, 0, 1}, xColor);  // YZ plane = X rotation
        drawCircle({1, 0, 0}, {0, 0, 1}, yColor);  // XZ plane = Y rotation
        drawCircle({1, 0, 0}, {0, 1, 0}, zColor);  // XY plane = Z rotation
    }

    flush(view, proj, fbWidth, fbHeight);
}

void GizmoRenderer::renderLightGizmos(engine::ecs::Registry& registry, const glm::mat4& view,
                                      const glm::mat4& proj, uint16_t fbWidth, uint16_t fbHeight)
{
    using namespace engine::rendering;

    if (!initialized_)
        return;

    glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
    cameraPos_ = camPos;

    // --- Directional lights ---
    auto dirView = registry.view<DirectionalLightComponent, WorldTransformComponent>();
    dirView.each(
        [&](engine::ecs::EntityID /*entity*/, const DirectionalLightComponent& dl,
            const WorldTransformComponent& wt)
        {
            glm::vec3 pos = glm::vec3(wt.matrix[3]);
            glm::vec3 dir =
                glm::normalize(glm::vec3(dl.direction.x, dl.direction.y, dl.direction.z));

            // Convert light color to ABGR uint32_t.
            uint32_t color =
                0xFF000000u |
                (static_cast<uint32_t>(glm::clamp(dl.color.z, 0.0f, 1.0f) * 255.0f) << 16) |
                (static_cast<uint32_t>(glm::clamp(dl.color.y, 0.0f, 1.0f) * 255.0f) << 8) |
                (static_cast<uint32_t>(glm::clamp(dl.color.x, 0.0f, 1.0f) * 255.0f));

            // Scale by camera distance for consistent screen size.
            float dist = glm::length(camPos - pos);
            float scale = dist * 0.1f;

            // Find two perpendicular vectors to dir.
            glm::vec3 perp1;
            if (std::abs(dir.y) < 0.99f)
            {
                perp1 = glm::normalize(glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f)));
            }
            else
            {
                perp1 = glm::normalize(glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f)));
            }
            glm::vec3 perp2 = glm::cross(dir, perp1);

            // Draw circle perpendicular to direction (12 segments).
            constexpr int kCircleSegments = 12;
            constexpr float kTwoPi = 2.0f * 3.14159265f;
            float radius = scale * 0.3f;
            for (int i = 0; i < kCircleSegments; ++i)
            {
                float a0 = static_cast<float>(i) * kTwoPi / static_cast<float>(kCircleSegments);
                float a1 = static_cast<float>(i + 1) * kTwoPi / static_cast<float>(kCircleSegments);
                glm::vec3 p0 = pos + (perp1 * std::cos(a0) + perp2 * std::sin(a0)) * radius;
                glm::vec3 p1 = pos + (perp1 * std::cos(a1) + perp2 * std::sin(a1)) * radius;
                drawLine(p0, p1, color);
            }

            // Draw 6 arrows radiating outward from the circle in the light direction.
            constexpr int kArrowCount = 6;
            float arrowLen = scale * 0.5f;
            for (int i = 0; i < kArrowCount; ++i)
            {
                float angle = static_cast<float>(i) * kTwoPi / static_cast<float>(kArrowCount);
                glm::vec3 base = pos + (perp1 * std::cos(angle) + perp2 * std::sin(angle)) * radius;
                glm::vec3 tip = base + dir * arrowLen;
                drawLine(base, tip, color);

                // Small arrowhead at tip.
                float headLen = arrowLen * 0.2f;
                float headRad = arrowLen * 0.08f;
                glm::vec3 headBase = tip - dir * headLen;
                for (int j = 0; j < 4; ++j)
                {
                    float ha = static_cast<float>(j) * 3.14159265f * 0.5f;
                    glm::vec3 offset = (perp1 * std::cos(ha) + perp2 * std::sin(ha)) * headRad;
                    drawLine(tip, headBase + offset, color);
                }
            }
        });

    // --- Point lights ---
    auto ptView = registry.view<PointLightComponent, WorldTransformComponent>();
    ptView.each(
        [&](engine::ecs::EntityID /*entity*/, const PointLightComponent& pl,
            const WorldTransformComponent& wt)
        {
            glm::vec3 pos = glm::vec3(wt.matrix[3]);

            // Convert light color to ABGR uint32_t.
            uint32_t color =
                0xFF000000u |
                (static_cast<uint32_t>(glm::clamp(pl.color.z, 0.0f, 1.0f) * 255.0f) << 16) |
                (static_cast<uint32_t>(glm::clamp(pl.color.y, 0.0f, 1.0f) * 255.0f) << 8) |
                (static_cast<uint32_t>(glm::clamp(pl.color.x, 0.0f, 1.0f) * 255.0f));

            // Scale by camera distance for consistent screen size.
            float dist = glm::length(camPos - pos);
            float scale = dist * 0.08f;

            // Draw a diamond / cross shape at the entity's position.
            drawLine(pos - glm::vec3(scale, 0, 0), pos + glm::vec3(scale, 0, 0), color);
            drawLine(pos - glm::vec3(0, scale, 0), pos + glm::vec3(0, scale, 0), color);
            drawLine(pos - glm::vec3(0, 0, scale), pos + glm::vec3(0, 0, scale), color);

            // Draw a circle showing the light's radius.
            constexpr int kCircleSegments = 16;
            constexpr float kTwoPi = 2.0f * 3.14159265f;
            for (int i = 0; i < kCircleSegments; ++i)
            {
                float a0 = static_cast<float>(i) * kTwoPi / static_cast<float>(kCircleSegments);
                float a1 = static_cast<float>(i + 1) * kTwoPi / static_cast<float>(kCircleSegments);
                // XZ plane circle at the light's radius.
                glm::vec3 p0 = pos + glm::vec3(std::cos(a0), 0, std::sin(a0)) * pl.radius;
                glm::vec3 p1 = pos + glm::vec3(std::cos(a1), 0, std::sin(a1)) * pl.radius;
                drawLine(p0, p1, color);
            }
        });

    flush(view, proj, fbWidth, fbHeight);
}

}  // namespace engine::editor
