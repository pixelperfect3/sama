#include "editor/gizmo/GizmoRenderer.h"

#include <bgfx/bgfx.h>

#include <cmath>
#include <glm/gtc/type_ptr.hpp>

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
    program_ = engine::rendering::loadGizmoProgram();

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
    if (lineVertCount_ + 6 > kMaxLineVerts)
        return;

    // Build a camera-facing quad for the line segment.
    glm::vec3 lineDir = to - from;
    float lineLen = glm::length(lineDir);
    if (lineLen < 1e-6f)
        return;
    lineDir /= lineLen;

    glm::vec3 midpoint = (from + to) * 0.5f;
    glm::vec3 toCamera = glm::normalize(cameraPos_ - midpoint);
    glm::vec3 side = glm::normalize(glm::cross(lineDir, toCamera));

    // Scale width by distance to camera for roughly constant screen-space width.
    float dist = glm::length(cameraPos_ - midpoint);
    float halfW = kLineWidth * dist * 0.5f;
    glm::vec3 offset = side * halfW;

    // 4 corners.
    glm::vec3 v0 = from - offset;
    glm::vec3 v1 = from + offset;
    glm::vec3 v2 = to + offset;
    glm::vec3 v3 = to - offset;

    // 2 triangles (6 vertices).
    lineVerts_[lineVertCount_++] = {v0.x, v0.y, v0.z, color};
    lineVerts_[lineVertCount_++] = {v1.x, v1.y, v1.z, color};
    lineVerts_[lineVertCount_++] = {v2.x, v2.y, v2.z, color};
    lineVerts_[lineVertCount_++] = {v0.x, v0.y, v0.z, color};
    lineVerts_[lineVertCount_++] = {v2.x, v2.y, v2.z, color};
    lineVerts_[lineVertCount_++] = {v3.x, v3.y, v3.z, color};
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

    // Render as triangles (quads), no depth test, no backface culling, alpha blend.
    uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

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

}  // namespace engine::editor
