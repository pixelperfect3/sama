#pragma once

#include <bgfx/bgfx.h>

#include <glm/glm.hpp>

#include "editor/gizmo/TransformGizmo.h"

namespace engine::editor
{

// ---------------------------------------------------------------------------
// GizmoRenderer -- renders transform gizmo geometry using bgfx.
//
// Uses simple colored lines rendered as an overlay (no depth test against
// the scene).  Uses a dedicated bgfx view ID.
// ---------------------------------------------------------------------------

class GizmoRenderer
{
public:
    GizmoRenderer() = default;
    ~GizmoRenderer();

    // Initialize bgfx resources (vertex layout, program).
    void init();

    // Destroy bgfx resources.
    void shutdown();

    // Render the gizmo for the current frame.
    void render(const TransformGizmo& gizmo, const glm::mat4& view, const glm::mat4& proj,
                uint16_t fbWidth, uint16_t fbHeight);

private:
    // Submit a colored line as two vertices.
    void drawLine(const glm::vec3& from, const glm::vec3& to, uint32_t color);

    // Draw an arrow (line + cone tip approximated by lines).
    void drawArrow(const glm::vec3& origin, const glm::vec3& dir, float length, uint32_t color);

    // Submit all buffered lines to bgfx.
    void flush(const glm::mat4& view, const glm::mat4& proj, uint16_t fbWidth, uint16_t fbHeight);

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
    bool initialized_ = false;

    // Line vertex buffer (transient, rebuilt each frame).
    struct LineVertex
    {
        float x, y, z;
        uint32_t abgr;
    };

    static constexpr uint32_t kMaxLineVerts = 512;
    LineVertex lineVerts_[kMaxLineVerts];
    uint32_t lineVertCount_ = 0;

    // View ID for gizmo overlay rendering.
    static constexpr bgfx::ViewId kGizmoView = 50;
};

}  // namespace engine::editor
