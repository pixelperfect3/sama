#pragma once

#if defined(__APPLE__)
// TargetConditionals is Apple-only — guards the iOS macro defined below.
// On Android / Linux / Windows this header doesn't exist, so we skip it
// and the TARGET_OS_IPHONE check naturally evaluates to false (it's only
// referenced inside the #if defined(__APPLE__) branch).
#include <TargetConditionals.h>
#endif

#include <cstdint>
#include <memory>

#include "engine/input/InputState.h"
#include "engine/memory/FrameArena.h"
#include "engine/platform/Window.h"
#include "engine/rendering/HandleTypes.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShadowRenderer.h"

// Platform forward-declarations.  We branch three ways: desktop (GLFW),
// Android (android_app + AInputEvent), iOS (the engine::platform::ios types).
// TargetConditionals.h exists on Apple platforms only, so we guard the iOS
// helper macro.
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define ENGINE_IS_IOS 1
#else
#define ENGINE_IS_IOS 0
#endif

#if !defined(__ANDROID__) && !ENGINE_IS_IOS
struct GLFWwindow;
#elif defined(__ANDROID__)
struct android_app;
struct AInputEvent;
#endif

namespace engine::input
{
class InputSystem;
class IInputBackend;
}  // namespace engine::input

#ifdef __ANDROID__
namespace engine::platform
{
class AndroidWindow;
class AndroidGyro;
class AndroidFileSystem;
}  // namespace engine::platform
#endif

#if ENGINE_IS_IOS
namespace engine::platform::ios
{
class IosWindow;
class IosTouchInput;
class IosGyro;
class IosFileSystem;
}  // namespace engine::platform::ios
#endif

#if ENGINE_IS_IOS || defined(__ANDROID__)
namespace engine::audio
{
class IAudioEngine;
}  // namespace engine::audio
#endif

namespace engine::core
{

// ---------------------------------------------------------------------------
// EngineDesc -- configuration for Engine::init().
// ---------------------------------------------------------------------------

struct EngineDesc
{
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
    const char* windowTitle = "Sama";
    uint32_t shadowResolution = 2048;
    uint32_t shadowCascades = 1;
    size_t frameArenaSize = 2 * 1024 * 1024;  // 2 MB
};

// ---------------------------------------------------------------------------
// Engine -- owns the window, renderer, default textures, shaders, ImGui, input,
// shadow renderer, and frame arena.  Provides beginFrame/endFrame to bracket
// each game-loop iteration.
//
// The Engine does NOT own game logic, physics, audio, animation systems, or
// the ECS registry -- those are managed by the application.
//
// On Android, the Engine uses ANativeWindow instead of GLFW and skips ImGui.
// The public API (resources, inputState, shaders, fbWidth/fbHeight, etc.)
// is identical on both platforms, so IGame implementations work unchanged.
// ---------------------------------------------------------------------------

class Engine
{
public:
    Engine();
    ~Engine();

    // Non-copyable, non-movable.
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

#if !defined(__ANDROID__) && !ENGINE_IS_IOS
    // Initialize all subsystems (desktop).  Must be called before anything else.
    // Returns false on failure (window or renderer creation failed).
    bool init(const EngineDesc& desc);
#elif defined(__ANDROID__)
    // Initialize all subsystems (Android).  Must be called after the native
    // window is ready.  Returns false on failure.
    bool initAndroid(struct android_app* app, const EngineDesc& desc);
#else
    // Initialize all subsystems (iOS).  Must be called after the IosWindow
    // has been attached to its UIWindow (window->isReady() == true).  The
    // platform layer owners (window/touch/gyro/fs) outlive the Engine.
    //
    // Mirrors initAndroid in shape: takes the platform pieces by pointer and
    // wires them into the Engine's input / shadow / renderer subsystems.
    // The IosFileSystem pointer is currently advisory — kept on the Engine so
    // future subsystems (asset streaming, scene serialiser) can pull it in
    // without re-querying NSBundle.
    bool initIos(platform::ios::IosWindow* window, platform::ios::IosTouchInput* touch,
                 platform::ios::IosGyro* gyro, platform::ios::IosFileSystem* fs,
                 const EngineDesc& desc);
#endif

    // Shutdown all subsystems in correct order.
    void shutdown();

    // Frame lifecycle -- call in order each frame.
    //
    // beginFrame: polls window events, handles resize, computes dt, updates
    //             input state.  On desktop also feeds ImGui state and begins
    //             the ImGui frame.  Returns false if the app should close.
    bool beginFrame(float& outDt);

    // endFrame: on desktop calls imguiEndFrame; on all platforms resets the
    //           frame arena and calls renderer.endFrame (submits bgfx frame).
    void endFrame();

    // ----- Subsystem accessors -----

#if !defined(__ANDROID__) && !ENGINE_IS_IOS
    [[nodiscard]] platform::IWindow& window()
    {
        return *window_;
    }
#endif
    [[nodiscard]] rendering::Renderer& renderer()
    {
        return renderer_;
    }
    [[nodiscard]] rendering::RenderResources& resources()
    {
        return resources_;
    }
    [[nodiscard]] rendering::ShadowRenderer& shadow()
    {
        return shadow_;
    }
    [[nodiscard]] const rendering::ShaderUniforms& uniforms() const
    {
        return renderer_.uniforms();
    }
    [[nodiscard]] input::InputState& inputState()
    {
        return inputState_;
    }
    [[nodiscard]] const input::InputState& inputState() const
    {
        return inputState_;
    }
    [[nodiscard]] memory::FrameArena& frameArena()
    {
        return *frameArena_;
    }

#if ENGINE_IS_IOS || defined(__ANDROID__)
    // Audio engine — owned by Engine on mobile platforms so games get a
    // working IAudioEngine without having to construct one themselves.
    // Backed by SoLoud's miniaudio backend (CoreAudio on Apple, AAudio /
    // OpenSL ES on Android — miniaudio auto-selects).  Falls back to
    // NullAudioEngine if SoLoud init fails (e.g. simulator without an
    // audio route).  Always non-null after initIos() / initAndroid()
    // succeeds.
    [[nodiscard]] audio::IAudioEngine& audio()
    {
        return *audio_;
    }
    [[nodiscard]] const audio::IAudioEngine& audio() const
    {
        return *audio_;
    }
#endif

    // ----- Shader programs (loaded once at init) -----

    [[nodiscard]] rendering::ProgramHandle pbrProgram() const
    {
        return pbrProg_;
    }
    [[nodiscard]] rendering::ProgramHandle shadowProgram() const
    {
        return shadowProg_;
    }
    [[nodiscard]] rendering::ProgramHandle skinnedPbrProgram() const
    {
        return skinnedPbrProg_;
    }
    [[nodiscard]] rendering::ProgramHandle skinnedShadowProgram() const
    {
        return skinnedShadowProg_;
    }

    // ----- ImGui state -----

    [[nodiscard]] bool imguiWantsMouse() const;

    // ----- View 0 clear color -----

    // Set the RGBA clear color for view 0 (default: 0x303030FF).
    // Call this instead of bgfx::setViewClear directly.
    void setClearColor(uint32_t rgba);

    // ----- Framebuffer dimensions (physical pixels) -----

    [[nodiscard]] uint16_t fbWidth() const
    {
        return fbW_;
    }
    [[nodiscard]] uint16_t fbHeight() const
    {
        return fbH_;
    }

#if !defined(__ANDROID__) && !ENGINE_IS_IOS
    // ----- GLFW handle (needed for scroll callbacks, cursor capture, etc.) -----

    [[nodiscard]] GLFWwindow* glfwHandle() const
    {
        return glfwHandle_;
    }

    // Content scale factors for DPI-aware cursor coordinate conversion.
    [[nodiscard]] float contentScaleX() const
    {
        return contentScaleX_;
    }
    [[nodiscard]] float contentScaleY() const
    {
        return contentScaleY_;
    }

    // Accumulated ImGui scroll value.  Scroll callbacks should add to this.
    float& imguiScrollAccum()
    {
        return imguiScrollF_;
    }
#else
    // Content scale factor from the platform display density (Android dpi /
    // iOS UIScreen.nativeScale).
    [[nodiscard]] float contentScaleX() const
    {
        return contentScaleX_;
    }
    [[nodiscard]] float contentScaleY() const
    {
        return contentScaleY_;
    }
#endif

private:
    bool initialized_ = false;

#if !defined(__ANDROID__) && !ENGINE_IS_IOS
    // Window (desktop)
    std::unique_ptr<platform::IWindow> window_;
    GLFWwindow* glfwHandle_ = nullptr;
#elif defined(__ANDROID__)
    // Android app handle and platform objects
    struct android_app* androidApp_ = nullptr;
    std::unique_ptr<platform::AndroidWindow> androidWindow_;
    std::unique_ptr<platform::AndroidGyro> androidGyro_;
    std::unique_ptr<platform::AndroidFileSystem> androidFileSystem_;
    bool focused_ = false;

    // Activity lifecycle flag.  Toggled by APP_CMD_PAUSE / APP_CMD_RESUME.
    // While paused: rendering loop blocks on ALooper_pollAll, audio is
    // paused via SoLoud::setPauseAll, gyro polling is disabled.  See
    // engine/core/Engine.cpp handleAndroidCmd() for the lifecycle table
    // and docs/ANDROID_SUPPORT.md Phase B for the design rationale.
    bool paused_ = false;
#else
    // iOS platform objects — owned by IosApp / the application delegate, not
    // by the Engine.  The Engine holds back-pointers so beginFrame/endFrame
    // can drain touch / gyro state into the InputState and rebind bgfx if
    // the CAMetalLayer ever changes (e.g. after backgrounding).
    platform::ios::IosWindow* iosWindow_ = nullptr;
    platform::ios::IosTouchInput* iosTouch_ = nullptr;
    platform::ios::IosGyro* iosGyro_ = nullptr;
    platform::ios::IosFileSystem* iosFileSystem_ = nullptr;
#endif

    // Renderer
    rendering::Renderer renderer_;
    rendering::RenderResources resources_;
    rendering::ShadowRenderer shadow_;

    // Shader programs — store as the engine's bgfx-free ProgramHandle so the
    // public getter is a no-op return.  Boundary conversion to bgfx happens
    // in Engine.cpp's destroy path (and any other place that still talks to
    // bgfx directly).
    rendering::ProgramHandle pbrProg_ = rendering::kInvalidProgram;
    rendering::ProgramHandle shadowProg_ = rendering::kInvalidProgram;
    rendering::ProgramHandle skinnedPbrProg_ = rendering::kInvalidProgram;
    rendering::ProgramHandle skinnedShadowProg_ = rendering::kInvalidProgram;

    // Input
    std::unique_ptr<input::IInputBackend> inputBackend_;
    std::unique_ptr<input::InputSystem> inputSys_;
    input::InputState inputState_;

    // Frame arena
    std::unique_ptr<memory::FrameArena> frameArena_;

#if ENGINE_IS_IOS || defined(__ANDROID__)
    // Audio engine (iOS / Android — desktop apps construct their own
    // SoLoudAudioEngine since they have full control over the lifecycle).
    // Always non-null after initIos() / initAndroid() succeeds, even if
    // SoLoud failed to open an output device (we fall back to
    // NullAudioEngine in that case so games can call audio() unconditionally).
    std::unique_ptr<audio::IAudioEngine> audio_;
#endif

    // DPI content scale
    float contentScaleX_ = 1.f;
    float contentScaleY_ = 1.f;

#if !defined(__ANDROID__) && !ENGINE_IS_IOS
    // ImGui scroll accumulator (desktop only — ImGui isn't built on mobile).
    float imguiScrollF_ = 0.f;
#endif

    // View 0 clear color (RGBA)
    uint32_t clearColor_ = 0x303030FF;

    // Frame timing
    double prevTime_ = 0.0;
    uint16_t fbW_ = 0;
    uint16_t fbH_ = 0;

#ifdef __ANDROID__
    // Android command/input callbacks (static, forwarded to Engine via userData)
    static void handleAndroidCmd(struct android_app* app, int32_t cmd);
    static int32_t handleAndroidInput(struct android_app* app, ::AInputEvent* event);
#endif
};

}  // namespace engine::core
