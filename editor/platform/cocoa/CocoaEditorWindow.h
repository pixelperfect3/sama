#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "editor/platform/IEditorWindow.h"

namespace engine::editor
{

class CocoaHierarchyView;
class CocoaPropertiesView;
class CocoaConsoleView;
class CocoaResourceView;
class CocoaAnimationView;

// ---------------------------------------------------------------------------
// AndroidBuildSettings -- persisted Android build configuration.
// Mirrors the Build > Android > Settings… dialog and is stored in
// NSUserDefaults under the editor's bundle identifier.
// ---------------------------------------------------------------------------
struct AndroidBuildSettings
{
    std::string defaultTier = "mid";          // "low" | "mid" | "high"
    std::string keystorePath;                 // empty = debug keystore
    std::string keystorePasswordEnvVar;       // env var name (recommended)
    std::string outputApkPath;                // empty = build/android/Game.apk
    std::string packageId = "com.sama.game";  // matches AndroidManifest default
    std::string lastDeviceSerial;             // adb -s <serial> for picking device
    bool buildAndRun = false;                 // run after build by default
};

// ---------------------------------------------------------------------------
// CocoaEditorWindow -- native macOS window backed by NSWindow + NSSplitView
// layout with native panels and a central CAMetalLayer viewport.
//
// Pimpl pattern: no Cocoa/AppKit headers leak into this header.
// ---------------------------------------------------------------------------

class CocoaEditorWindow final : public IEditorWindow
{
public:
    CocoaEditorWindow();
    ~CocoaEditorWindow() override;

    // Non-copyable, non-movable.
    CocoaEditorWindow(const CocoaEditorWindow&) = delete;
    CocoaEditorWindow& operator=(const CocoaEditorWindow&) = delete;
    CocoaEditorWindow(CocoaEditorWindow&&) = delete;
    CocoaEditorWindow& operator=(CocoaEditorWindow&&) = delete;

    bool init(uint32_t width, uint32_t height, const char* title) override;
    void shutdown() override;

    bool shouldClose() const override;
    void pollEvents() override;

    void* nativeHandle() const override;
    void* nativeLayer() const override;

    uint32_t width() const override;
    uint32_t height() const override;
    uint32_t framebufferWidth() const override;
    uint32_t framebufferHeight() const override;
    float contentScale() const override;

    double mouseX() const override;
    double mouseY() const override;
    double mouseDeltaX() const override;
    double mouseDeltaY() const override;
    double scrollDeltaY() const override;
    bool isLeftMouseDown() const override;
    bool isRightMouseDown() const override;
    bool isKeyPressed(uint8_t keyCode) const override;

    bool isCommandDown() const override;
    bool isShiftDown() const override;
    bool isControlDown() const override;
    bool isOptionDown() const override;

    std::string showSaveDialog(const char* defaultName, const char* extension) override;
    std::string showOpenDialog(const char* extension) override;
    std::string showOpenDialogMultiExt(const std::vector<std::string>& extensions,
                                       const char* title) override;
    std::string showImportDialog() override;
    void setWindowTitle(const char* title) override;

    // --- Viewport-specific dimensions ----------------------------------------
    // These return the size of just the center 3D viewport panel,
    // not the entire window.

    uint32_t viewportWidth() const;
    uint32_t viewportHeight() const;
    uint32_t viewportFramebufferWidth() const;
    uint32_t viewportFramebufferHeight() const;

    // Returns true if the mouse is currently over the 3D viewport panel.
    bool isMouseOverViewport() const;

    // Menu action callback — called when a menu item is clicked.
    // Action is a string like "save_scene", "undo", "create_cube".
    using MenuCallback = void (*)(const char* action);
    void setMenuCallback(MenuCallback callback);

    // --- Android build status bar -------------------------------------------
    //
    // The status bar lives at the bottom of the window (below the bottom
    // tabbed panel) and is the editor's single source of truth for "what is
    // the Android APK build doing right now". `setBuildStatus(text, active)`
    // updates the label and toggles the indeterminate spinner. When
    // `succeeded` is set (build finished), the spinner stops and the cancel
    // button hides. Safe to call from any thread — internally trampolines
    // onto the main queue.
    enum class BuildStatusKind
    {
        Idle,     // No build running. Cancel hidden, spinner hidden.
        Running,  // Build in progress. Spinner spinning, cancel visible.
        Success,  // Build finished OK. Spinner stopped, cancel hidden.
        Failure,  // Build failed. Spinner stopped, cancel hidden, red text.
    };
    void setBuildStatus(const char* text, BuildStatusKind kind);

    // Set the handler invoked when the user clicks the Cancel button in
    // the status bar. Pass `nullptr` to clear.
    using BuildCancelHandler = std::function<void()>;
    void setBuildCancelHandler(BuildCancelHandler handler);

    // --- Android build settings (NSUserDefaults persistence) ----------------
    AndroidBuildSettings loadAndroidBuildSettings() const;
    void saveAndroidBuildSettings(const AndroidBuildSettings& settings);

    // Show a modal Android build settings sheet.  Returns true if the user
    // saved changes, false if cancelled.  On save, the new values are
    // persisted via `saveAndroidBuildSettings` and `outSettings` is
    // overwritten with the new values.
    bool showAndroidBuildSettingsDialog(AndroidBuildSettings& outSettings);

    // --- Android device discovery -------------------------------------------
    //
    // Lightweight helpers that shell out to `adb`. Kept on the window class
    // because their main consumer is the dialog (device picker) and the
    // status bar (no-device alert) — both Cocoa code.
    struct AdbDevice
    {
        std::string serial;       // device serial
        std::string description;  // human-readable model/state
    };
    std::vector<AdbDevice> queryAdbDevices() const;

    // Show a simple alert (OK button only).  Used by Build & Run when no
    // device is connected to point the user at install instructions.
    void showAlert(const char* title, const char* message);

    // --- Native panel views --------------------------------------------------
    CocoaHierarchyView* hierarchyView() const;
    CocoaPropertiesView* propertiesView() const;
    CocoaConsoleView* consoleView() const;
    CocoaResourceView* resourceView() const;
    CocoaAnimationView* animationView() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::editor
