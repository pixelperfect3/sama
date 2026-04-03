#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>

#include "engine/input/InputState.h"
#include "engine/memory/FrameArena.h"
#include "engine/platform/Window.h"
#include "engine/rendering/RenderResources.h"
#include "engine/rendering/Renderer.h"
#include "engine/rendering/ShadowRenderer.h"

struct GLFWwindow;

namespace engine::input
{
class InputSystem;
class IInputBackend;
}  // namespace engine::input

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

    // Initialize all subsystems.  Must be called before anything else.
    // Returns false on failure (window or renderer creation failed).
    bool init(const EngineDesc& desc);

    // Shutdown all subsystems in correct order.
    void shutdown();

    // Frame lifecycle -- call in order each frame.
    //
    // beginFrame: polls window events, handles resize, computes dt, updates
    //             input state, feeds ImGui cursor/button/scroll/keyboard state,
    //             begins ImGui frame.  Returns false if the window should close.
    bool beginFrame(float& outDt);

    // endFrame: calls imguiEndFrame, resets the frame arena, calls
    //           renderer.endFrame (submits bgfx frame).
    void endFrame();

    // ----- Subsystem accessors -----

    [[nodiscard]] platform::IWindow& window()
    {
        return *window_;
    }
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

    // ----- Shader programs (loaded once at init) -----

    [[nodiscard]] bgfx::ProgramHandle pbrProgram() const
    {
        return pbrProg_;
    }
    [[nodiscard]] bgfx::ProgramHandle shadowProgram() const
    {
        return shadowProg_;
    }
    [[nodiscard]] bgfx::ProgramHandle skinnedPbrProgram() const
    {
        return skinnedPbrProg_;
    }
    [[nodiscard]] bgfx::ProgramHandle skinnedShadowProgram() const
    {
        return skinnedShadowProg_;
    }

    // ----- ImGui state -----

    [[nodiscard]] bool imguiWantsMouse() const;

    // ----- Framebuffer dimensions (physical pixels) -----

    [[nodiscard]] uint16_t fbWidth() const
    {
        return fbW_;
    }
    [[nodiscard]] uint16_t fbHeight() const
    {
        return fbH_;
    }

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

private:
    bool initialized_ = false;

    // Window
    std::unique_ptr<platform::IWindow> window_;
    GLFWwindow* glfwHandle_ = nullptr;

    // Renderer
    rendering::Renderer renderer_;
    rendering::RenderResources resources_;
    rendering::ShadowRenderer shadow_;

    // Shader programs
    bgfx::ProgramHandle pbrProg_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shadowProg_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinnedPbrProg_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle skinnedShadowProg_ = BGFX_INVALID_HANDLE;

    // Default textures (owned by Engine, destroyed on shutdown)
    bgfx::TextureHandle whiteTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle neutralNormalTex_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteCubeTex_ = BGFX_INVALID_HANDLE;

    // Input
    std::unique_ptr<input::IInputBackend> inputBackend_;
    std::unique_ptr<input::InputSystem> inputSys_;
    input::InputState inputState_;

    // Frame arena
    std::unique_ptr<memory::FrameArena> frameArena_;

    // DPI content scale
    float contentScaleX_ = 1.f;
    float contentScaleY_ = 1.f;

    // ImGui scroll accumulator
    float imguiScrollF_ = 0.f;

    // Frame timing
    double prevTime_ = 0.0;
    uint16_t fbW_ = 0;
    uint16_t fbH_ = 0;
};

}  // namespace engine::core
