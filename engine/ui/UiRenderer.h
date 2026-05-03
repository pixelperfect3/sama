#pragma once

#include <cstdint>
#include <memory>

#include "engine/rendering/HandleTypes.h"

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
//
// All bgfx types live behind a private Impl (pImpl) so the public header
// stays bgfx-free; consumers (apps, games) only ever touch the public
// init/shutdown/render API plus the engine-wrapped ViewId.
// ---------------------------------------------------------------------------

class UiRenderer
{
public:
    UiRenderer();
    ~UiRenderer();

    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;
    UiRenderer(UiRenderer&&) noexcept;
    UiRenderer& operator=(UiRenderer&&) noexcept;

    void init();
    void shutdown();

    // Render all draw commands for this frame.
    // viewId: engine view to render on (e.g., kViewGameUi = 48)
    // screenW/H: logical screen size for orthographic projection
    void render(const UiDrawList& drawList, engine::rendering::ViewId viewId, uint16_t screenW,
                uint16_t screenH);

private:
    // pImpl — owns bgfx::ProgramHandle / VertexLayout / UniformHandle /
    // TextureHandle members.  Defined entirely in UiRenderer.cpp.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::ui
