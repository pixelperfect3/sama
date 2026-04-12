#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace engine::ui
{

class UiDrawList;

// ---------------------------------------------------------------------------
// UiRenderer
//
// Renders UiDrawList draw commands as batched 2D quads via bgfx transient
// buffers.  Uses the existing sprite shader (vs_sprite/fs_sprite) and
// the same vertex layout as SpriteBatcher: {float2 pos, float2 uv, uint8x4
// color} = 20 bytes/vertex.
//
// Sets up an orthographic projection on the given view so that coordinates
// map directly to logical screen pixels: (0,0) = top-left, (w,h) = bottom-
// right.  Text commands are skipped (Phase 4).
// ---------------------------------------------------------------------------

class UiRenderer
{
public:
    void init();
    void shutdown();

    // Render all draw commands for this frame.
    // viewId: bgfx view to render on (e.g., kViewGameUi = 48)
    // screenW/H: logical screen size for orthographic projection
    void render(const UiDrawList& drawList, bgfx::ViewId viewId, uint16_t screenW,
                uint16_t screenH);

private:
    // Slug submission path — one draw call per glyph because the per-glyph
    // curve range must be set as a uniform. Defined in UiRenderer.cpp.
    void renderSlugText(const struct UiDrawCmd& cmd, const class SlugFont* font,
                        bgfx::ProgramHandle prog, bgfx::ViewId viewId, uint64_t blendState);

    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_;
    bgfx::UniformHandle s_texture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteTex_ = BGFX_INVALID_HANDLE;

    // Rounded-rect path: own program + own vertex layout (extra vec4
    // attribute carrying half-size + corner radius). Used only when a
    // Rect command has cornerRadius > 0.
    bgfx::ProgramHandle roundedProgram_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout roundedLayout_;
};

}  // namespace engine::ui
