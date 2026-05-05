#pragma once

#include <bgfx/bgfx.h>

#include <glm/glm.hpp>

#include "editor/gizmo/TransformGizmo.h"
#include "engine/ecs/Registry.h"

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

    // Render light gizmos for all light entities in the registry.
    void renderLightGizmos(engine::ecs::Registry& registry, const glm::mat4& view,
                           const glm::mat4& proj, uint16_t fbWidth, uint16_t fbHeight);

private:
    // Submit a thick line as a camera-facing quad (6 vertices, 2 triangles).
    void drawLine(const glm::vec3& from, const glm::vec3& to, uint32_t color);

    // Draw an arrow (line + cone tip approximated by lines).
    void drawArrow(const glm::vec3& origin, const glm::vec3& dir, float length, uint32_t color);

    // Submit all buffered geometry to bgfx.
    void flush(const glm::mat4& view, const glm::mat4& proj, uint16_t fbWidth, uint16_t fbHeight);

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
    bool initialized_ = false;

    // Quad vertex buffer (transient, rebuilt each frame).
    // Each "line" is a camera-facing quad (6 vertices).
    struct LineVertex
    {
        float x, y, z;
        uint32_t abgr;
    };

    static constexpr uint32_t kMaxLineVerts = 8192;
    LineVertex lineVerts_[kMaxLineVerts];
    uint32_t lineVertCount_ = 0;

    // Camera position for billboard computation — set each frame before drawing.
    glm::vec3 cameraPos_{0.0f};

    // Line width in world-space units (will be scaled by distance).
    static constexpr float kLineWidth = 0.012f;

    // View ID for gizmo overlay rendering.  Sits above kViewImGui (50) and
    // kViewUi (51) so editor gizmos render on top of the HUD overlay and the
    // game UI.  Pre-Phase-7 the engine UI lived at views 14/15 and gizmo
    // could safely use view 50; the unified post-process pipeline moved
    // engine UI into 48-51, so editor overlays must move past that range.
    static constexpr bgfx::ViewId kGizmoView = 52;
};

}  // namespace engine::editor
