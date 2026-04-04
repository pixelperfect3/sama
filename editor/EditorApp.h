#pragma once

#include <cstdint>
#include <memory>

namespace engine::editor
{

class IEditorWindow;

// ---------------------------------------------------------------------------
// EditorApp -- Phase 1 editor application.
//
// Owns the native Cocoa window, bgfx renderer, and a simple test scene
// (PBR-lit cube on a ground plane).  OrbitCamera for viewport navigation.
// No GLFW, no ImGui.
// ---------------------------------------------------------------------------

class EditorApp
{
public:
    EditorApp();
    ~EditorApp();

    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    // Initialize all subsystems.  Returns false on failure.
    bool init(uint32_t width = 1600, uint32_t height = 1000);

    // Run the main loop.  Blocks until the window is closed.
    void run();

    // Shutdown all subsystems.
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
