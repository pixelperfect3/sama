#define GLFW_INCLUDE_NONE
#include "engine/core/Engine.h"

#include <GLFW/glfw3.h>
#include <bgfx/bgfx.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "engine/input/InputSystem.h"
#include "engine/input/desktop/GlfwInputBackend.h"
#include "engine/platform/desktop/GlfwWindow.h"
#include "engine/rendering/ShaderLoader.h"
#include "engine/rendering/ViewIds.h"

namespace engine::core
{

Engine::Engine() = default;

Engine::~Engine()
{
    if (initialized_)
    {
        shutdown();
    }
}

bool Engine::init(const EngineDesc& desc)
{
    // -- Window -----------------------------------------------------------
    window_ = platform::createWindow({desc.windowWidth, desc.windowHeight, desc.windowTitle});
    if (!window_)
        return false;

    auto* glfwWin = static_cast<platform::GlfwWindow*>(window_.get());
    glfwHandle_ = glfwWin->glfwHandle();

    // -- Renderer ---------------------------------------------------------
    {
        rendering::RendererDesc rd;
        rd.nativeWindowHandle = window_->nativeWindowHandle();
        rd.nativeDisplayHandle = window_->nativeDisplayHandle();
        rd.width = desc.windowWidth;
        rd.height = desc.windowHeight;
        rd.headless = false;
        if (!renderer_.init(rd))
            return false;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);

    // -- Shader programs --------------------------------------------------
    pbrProg_ = rendering::loadPbrProgram();
    shadowProg_ = rendering::loadShadowProgram();
    skinnedPbrProg_ = rendering::loadSkinnedPbrProgram();
    skinnedShadowProg_ = rendering::loadSkinnedShadowProgram();

    // -- Default textures -------------------------------------------------
    const uint8_t kWhite[4] = {255, 255, 255, 255};
    whiteTex_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                      bgfx::copy(kWhite, sizeof(kWhite)));
    resources_.setWhiteTexture(whiteTex_);

    const uint8_t kNeutralNormal[4] = {128, 128, 255, 255};
    neutralNormalTex_ =
        bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                              bgfx::copy(kNeutralNormal, sizeof(kNeutralNormal)));
    resources_.setNeutralNormalTexture(neutralNormalTex_);

    uint8_t cubeFaces[6 * 4];
    for (int i = 0; i < 6 * 4; ++i)
        cubeFaces[i] = 255;
    whiteCubeTex_ =
        bgfx::createTextureCube(1, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
                                bgfx::copy(cubeFaces, sizeof(cubeFaces)));
    resources_.setWhiteCubeTexture(whiteCubeTex_);

    // -- Shadow renderer --------------------------------------------------
    {
        rendering::ShadowDesc sd;
        sd.resolution = desc.shadowResolution;
        sd.cascadeCount = desc.shadowCascades;
        shadow_.init(sd);
    }

    // -- ImGui ------------------------------------------------------------
    imguiCreate(16.f);

    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigWindowsMoveFromTitleBarOnly = true;

        io.KeyMap[ImGuiKey_UpArrow] = GLFW_KEY_UP;
        io.KeyMap[ImGuiKey_DownArrow] = GLFW_KEY_DOWN;
        io.KeyMap[ImGuiKey_LeftArrow] = GLFW_KEY_LEFT;
        io.KeyMap[ImGuiKey_RightArrow] = GLFW_KEY_RIGHT;
        io.KeyMap[ImGuiKey_PageUp] = GLFW_KEY_PAGE_UP;
        io.KeyMap[ImGuiKey_PageDown] = GLFW_KEY_PAGE_DOWN;
        io.KeyMap[ImGuiKey_Home] = GLFW_KEY_HOME;
        io.KeyMap[ImGuiKey_End] = GLFW_KEY_END;
        io.KeyMap[ImGuiKey_Enter] = GLFW_KEY_ENTER;
        io.KeyMap[ImGuiKey_Escape] = GLFW_KEY_ESCAPE;
        io.KeyMap[ImGuiKey_Space] = GLFW_KEY_SPACE;
        io.KeyMap[ImGuiKey_Backspace] = GLFW_KEY_BACKSPACE;
        io.KeyMap[ImGuiKey_Tab] = GLFW_KEY_TAB;

        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }

    glfwSetWindowUserPointer(glfwHandle_, this);
    glfwSetScrollCallback(glfwHandle_,
                          [](GLFWwindow* win, double /*xoff*/, double yoff)
                          {
                              auto* eng = static_cast<Engine*>(glfwGetWindowUserPointer(win));
                              if (eng)
                              {
                                  eng->imguiScrollAccum() += static_cast<float>(yoff);
                              }
                          });

    // -- DPI content scale ------------------------------------------------
    glfwGetWindowContentScale(glfwHandle_, &contentScaleX_, &contentScaleY_);

    // -- Input ------------------------------------------------------------
    inputBackend_ = std::make_unique<input::GlfwInputBackend>(glfwHandle_);
    inputSys_ = std::make_unique<input::InputSystem>(*inputBackend_);

    // -- Frame arena ------------------------------------------------------
    frameArena_ = std::make_unique<memory::FrameArena>(desc.frameArenaSize);

    // -- Timing -----------------------------------------------------------
    prevTime_ = glfwGetTime();

    // Flush initial resource uploads.
    renderer_.endFrame();

    initialized_ = true;
    return true;
}

void Engine::shutdown()
{
    if (!initialized_)
        return;

    imguiDestroy();

    shadow_.shutdown();

    if (bgfx::isValid(pbrProg_))
        bgfx::destroy(pbrProg_);
    if (bgfx::isValid(shadowProg_))
        bgfx::destroy(shadowProg_);
    if (bgfx::isValid(skinnedPbrProg_))
        bgfx::destroy(skinnedPbrProg_);
    if (bgfx::isValid(skinnedShadowProg_))
        bgfx::destroy(skinnedShadowProg_);

    if (bgfx::isValid(whiteTex_))
        bgfx::destroy(whiteTex_);
    if (bgfx::isValid(neutralNormalTex_))
        bgfx::destroy(neutralNormalTex_);
    if (bgfx::isValid(whiteCubeTex_))
        bgfx::destroy(whiteCubeTex_);

    resources_.destroyAll();

    renderer_.endFrame();
    renderer_.shutdown();

    // Reset unique_ptrs (input, frame arena).
    inputSys_.reset();
    inputBackend_.reset();
    frameArena_.reset();

    window_.reset();

    initialized_ = false;
}

bool Engine::beginFrame(float& outDt)
{
    if (window_->shouldClose())
        return false;

    // -- Timing -----------------------------------------------------------
    double now = glfwGetTime();
    outDt = static_cast<float>(std::min(now - prevTime_, 0.05));
    prevTime_ = now;

    // -- Events -----------------------------------------------------------
    window_->pollEvents();

    // -- Resize -----------------------------------------------------------
    int fbW, fbH;
    glfwGetFramebufferSize(glfwHandle_, &fbW, &fbH);
    if ((fbW != static_cast<int>(fbW_) || fbH != static_cast<int>(fbH_)) && fbW > 0 && fbH > 0)
    {
        renderer_.resize(static_cast<uint32_t>(fbW), static_cast<uint32_t>(fbH));
        fbW_ = static_cast<uint16_t>(fbW);
        fbH_ = static_cast<uint16_t>(fbH);
    }
    if (fbW <= 0 || fbH <= 0)
    {
        // Minimized -- still need to call endFrame to keep bgfx happy.
        renderer_.endFrame();
        outDt = 0.f;
        return true;  // not closing, just minimized
    }

    // -- Input ------------------------------------------------------------
    inputSys_->update(inputState_);

    // -- ImGui begin frame ------------------------------------------------
    {
        double mx, my;
        glfwGetCursorPos(glfwHandle_, &mx, &my);

        uint8_t imguiButtons = 0;
        if (glfwGetMouseButton(glfwHandle_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
            imguiButtons |= IMGUI_MBUT_LEFT;
        if (glfwGetMouseButton(glfwHandle_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
            imguiButtons |= IMGUI_MBUT_RIGHT;
        if (glfwGetMouseButton(glfwHandle_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
            imguiButtons |= IMGUI_MBUT_MIDDLE;

        // Feed keyboard nav state for ImGui.
        ImGuiIO& io = ImGui::GetIO();
        static const int kNavKeys[] = {GLFW_KEY_UP,        GLFW_KEY_DOWN, GLFW_KEY_PAGE_UP,
                                       GLFW_KEY_PAGE_DOWN, GLFW_KEY_HOME, GLFW_KEY_END};
        for (int k : kNavKeys)
            io.KeysDown[k] = (glfwGetKey(glfwHandle_, k) == GLFW_PRESS);

        imguiBeginFrame(static_cast<int32_t>(mx * contentScaleX_),
                        static_cast<int32_t>(my * contentScaleY_), imguiButtons,
                        static_cast<int32_t>(imguiScrollF_), fbW_, fbH_, -1, rendering::kViewImGui);
    }

    return true;
}

void Engine::endFrame()
{
    imguiEndFrame();

    if (frameArena_)
        frameArena_->reset();

    renderer_.endFrame();
}

bool Engine::imguiWantsMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

}  // namespace engine::core
