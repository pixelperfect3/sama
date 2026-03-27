#pragma once

#include <bgfx/bgfx.h>

#include <string>
#include <vector>

namespace engine::debug
{

// ---------------------------------------------------------------------------
// DebugTexturePanel — scrollable ImGui window showing texture thumbnails.
//
// Designed to be reusable by any sample app.  After registering textures via
// add(), call show() each frame between imguiBeginFrame() and imguiEndFrame().
//
// Typical usage:
//   // Once, after textures are uploaded to GPU:
//   panel.add(handle, "Albedo");
//   panel.add(handle2, "Normal");
//
//   // Each frame:
//   imguiBeginFrame(mx, my, buttons, scroll, w, h, -1, kViewImGui);
//   panel.show();
//   imguiEndFrame();
// ---------------------------------------------------------------------------

struct TextureEntry
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    std::string label;
};

class DebugTexturePanel
{
public:
    // Register a texture with a display label.  Call once per texture after
    // GPU upload.  The handle must remain valid for the panel's lifetime.
    void add(bgfx::TextureHandle handle, std::string label);

    // Remove all registered textures.
    void clear();

    // Render the ImGui window.  Must be called between imguiBeginFrame() and
    // imguiEndFrame().  thumbSize is the displayed square size in pixels.
    // Returns true if the ImGui window is hovered (use to suppress game input).
    bool show(float thumbSize = 256.f);

private:
    std::vector<TextureEntry> m_textures;
};

}  // namespace engine::debug
