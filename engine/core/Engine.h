#pragma once

#if defined(__APPLE__)
// TargetConditionals is Apple-only — guards the iOS macro defined below.
// On Android / Linux / Windows this header doesn't exist, so we skip it
// and the TARGET_OS_IPHONE check naturally evaluates to false (it's only
// referenced inside the #if defined(__APPLE__) branch).
#include <TargetConditionals.h>
#endif

#include <chrono>
#include <cstdint>
#include <functional>
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
// EngineFrameStats -- per-frame wall-clock CPU timings, updated by the
// Engine each frame.  Reads bgfx-level timings via the rendering::FrameStats
// API; this is the *outer* layer (everything the game thread spends between
// IGame callbacks, including bgfx::frame()'s vsync / GPU fence wait).
//
// All values are milliseconds for the most recent completed frame.  Cost of
// gathering these is ~5 chrono::steady_clock calls per frame — not
// measurable on a real-world frame budget.  Zero cost when not read.
//
// Reading: after Engine::endFrame returns, call engine.frameStats().  All
// fields refer to the frame that just ended.
//
// Common uses:
//   - HUD overlay (game logs "frame total = beginFrame + endFrame + ..."
//     so the user can see exactly where their 16.67 ms went).
//   - Regression detection (perf_smoke-style asserts).
//   - Bug reports — paste the breakdown directly into a perf ticket.
// ---------------------------------------------------------------------------

struct EngineFrameStats
{
    // Wall time for Engine::beginFrame() to run (input poll, ImGui begin,
    // view 0 clear/touch, lifecycle handling on Android, etc.).  Does NOT
    // include any IGame work, only engine plumbing.
    float beginFrameMs = 0.0F;

    // Wall time for Engine::endFrame() to run (ImGui end, frame arena
    // reset, then renderer_.endFrame()).  Includes postProcessSubmitMs and
    // bgfxFrameMs below — those are the two dominant sub-phases.
    float endFrameMs = 0.0F;

    // Subset of endFrameMs — Renderer's auto post-process / tonemap submit.
    // Cheap on a default-render-settings game (~50 us on Pixel 9); larger
    // when bloom / SSAO / FXAA are enabled in RenderSettings.
    float postProcessSubmitMs = 0.0F;

    // Subset of endFrameMs — bgfx::frame() wall time.  Meaning depends on
    // EngineDesc::singleThreaded:
    //   * Multi-threaded (the Sama default): asynchronous hand-off to the
    //     render thread via a lock-free ring; bgfx::frame() returns once the
    //     queue accepts the encoded frame.  Typically ~0.1 ms on Pixel 9.
    //   * Single-threaded: includes command-buffer recording AND vsync /
    //     GPU fence wait, all charged to the calling thread.  Typically
    //     ~10-20 ms on Pixel 9 — this is the cost that drove the move to
    //     multi-threaded as the default.
    float bgfxFrameMs = 0.0F;

    // Wall time from Engine::beginFrame entry → Engine::endFrame exit.
    // Useful as a sanity check vs the sum of the other fields, and as the
    // denominator for FPS math.  Game-side IGame::onFixedUpdate, onUpdate,
    // onRender all run between begin/end so their cost is INCLUDED here
    // (subtract the begin / end values to isolate the game-side budget).
    float fullFrameMs = 0.0F;
};

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

    // bgfx threading mode.  Default is multi-threaded (false) — bgfx spawns
    // its own render thread and bgfx::frame() is an asynchronous hand-off,
    // saving ~10-20 ms/frame on the game thread on Android (the dominant
    // cost in single-threaded mode is the GPU-fence/vsync wait charged to
    // the calling thread).  Set true for code paths that need to do work
    // (e.g. blit-and-readback) on the calling thread between frames, like
    // the screenshot fixture.  Forwarded to RendererDesc::singleThreaded.
    bool singleThreaded = false;

    // Gyroscope + accelerometer opt-in (Android).  Default false because the
    // sensors burn ~5-10 mW continuously even when the game never reads them
    // — a non-trivial fraction of standby power on low-tier phones.  Games
    // that actually consume `inputState().gyro()` must opt in by setting
    // this true; everything else gets the sensors disabled for the entire
    // process lifetime (pause/resume cycles included).  Today only Android
    // honours this flag; iOS still auto-enables in IosApp.mm pending the
    // same audit follow-up.  See docs/PERF_AUDIT_2026-05-25.md item #P1.
    bool enableGyro = false;
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

    // Per-frame wall-clock timings for the most recent completed frame.
    // See EngineFrameStats above for the field semantics.  Updated each
    // call to endFrame; zero-cost when not read.
    [[nodiscard]] const EngineFrameStats& frameStats() const
    {
        return frameStats_;
    }

#ifdef __ANDROID__
    // ----- Save / restore state (Android only) -----
    //
    // NativeActivity does not surface `onSaveInstanceState` /
    // `onRestoreInstanceState` to native code (those are Java-only callbacks
    // on `android.app.Activity`), so games that need cross-launch state
    // persist their own data via `android_app::activity->externalDataPath`.
    //
    // The callback registered here fires from `APP_CMD_SAVE_STATE` — Android
    // emits it on rotation, process kill, and "backgrounded long enough"
    // events.  Games should write their state synchronously inside the
    // callback (typical pattern: serialise a tiny POD blob with
    // `engine::platform::writeSavedState("game.state", ...)`).  Restore is
    // pull-based: the game calls `engine::platform::readSavedState(...)`
    // from `onInit` and applies whatever it gets back.
    //
    // The callback is invoked on the main / app-glue thread (the same
    // thread that calls beginFrame), so no locking is required between
    // the callback and the rest of the game state.
    void registerSaveStateCallback(std::function<void()> cb);

    // Returns true iff `APP_CMD_SAVE_STATE` has fired at least once since
    // init.  Useful when games need to distinguish "first launch" from
    // "restored launch" for telemetry / HUD purposes.  Mirrors the iOS
    // restored-from-state flag.
    [[nodiscard]] bool savedStateFired() const
    {
        return savedStateFired_;
    }
#endif

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

    // Latched copy of EngineDesc::enableGyro for the Android lifecycle path.
    // We can't just look at `androidGyro_->isEnabled()` to decide whether to
    // re-enable on APP_CMD_RESUME, because PAUSE has already flipped it to
    // false by then.  Keeping the original opt-in choice on the Engine lets
    // RESUME restore the pre-pause state without "always enable on resume"
    // (which is what caused the always-on bug in the first place — games
    // that never opted in still got the sensor flipped on after the first
    // pause/resume cycle).
    bool gyroOptedIn_ = false;

    // Save-state callback — registered by the game via
    // registerSaveStateCallback() and fired from APP_CMD_SAVE_STATE.  The
    // callback is expected to call engine::platform::writeSavedState()
    // synchronously to persist its small state blob under externalDataPath.
    // We do NOT use NativeActivity's `savedState` / `savedStateSize` slots
    // (those are kernel-shared-memory pages with a 4 KiB practical limit
    // and tricky lifetime rules) — a plain file is simpler and supports
    // larger payloads.
    std::function<void()> saveStateCallback_;

    // Set true on the first APP_CMD_SAVE_STATE this process sees.  Powers
    // savedStateFired() so games can show "restored" indicators.
    bool savedStateFired_ = false;
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

    // Per-frame timings; populated by beginFrame / endFrame.  See
    // EngineFrameStats at the top of this header for what each field
    // measures.  Public via frameStats() accessor.
    EngineFrameStats frameStats_;
    std::chrono::steady_clock::time_point frameBeginTime_;
    std::chrono::steady_clock::time_point endFrameStart_;

#ifdef __ANDROID__
    // Android command/input callbacks (static, forwarded to Engine via userData)
    static void handleAndroidCmd(struct android_app* app, int32_t cmd);
    static int32_t handleAndroidInput(struct android_app* app, ::AInputEvent* event);
#endif
};

}  // namespace engine::core
