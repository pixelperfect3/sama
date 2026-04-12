// UiTestApp -- comprehensive UI widget test application.
//
// Demonstrates all Sama UI widgets: UiPanel, UiText, UiButton, UiSlider,
// UiProgressBar, plus layout, event dispatch, toast notifications, and
// modal dialogs across four screens (Menu, HUD, Settings, Inventory).

#define GLFW_INCLUDE_NONE
#include "UiTestApp.h"

#include <GLFW/glfw3.h>
#include <bgfx/bgfx.h>
#include <mach-o/dyld.h>  // _NSGetExecutablePath

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Engine.h"
#include "engine/ecs/Registry.h"
#include "engine/input/InputState.h"
#include "engine/input/Key.h"
#include "engine/rendering/RenderPass.h"
#include "engine/rendering/ViewIds.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiEvent.h"
#include "engine/ui/widgets/UiButton.h"
#include "engine/ui/widgets/UiPanel.h"
#include "engine/ui/widgets/UiProgressBar.h"
#include "engine/ui/widgets/UiSlider.h"
#include "engine/ui/widgets/UiText.h"

using namespace engine::core;
using namespace engine::input;
using namespace engine::rendering;
using namespace engine::ui;

// =============================================================================
// Vertex for UI quad rendering
// =============================================================================

struct UiVertex
{
    float x, y;
    uint32_t abgr;
};

static bgfx::VertexLayout s_uiVertexLayout;
static bool s_layoutInitialized = false;

static void initVertexLayout()
{
    if (s_layoutInitialized)
    {
        return;
    }
    s_uiVertexLayout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    s_layoutInitialized = true;
}

// Convert RGBA float [0,1] to packed ABGR uint32.
static uint32_t toAbgr(const engine::math::Vec4& c)
{
    uint8_t r = static_cast<uint8_t>(std::clamp(c.x, 0.f, 1.f) * 255.f);
    uint8_t g = static_cast<uint8_t>(std::clamp(c.y, 0.f, 1.f) * 255.f);
    uint8_t b = static_cast<uint8_t>(std::clamp(c.z, 0.f, 1.f) * 255.f);
    uint8_t a = static_cast<uint8_t>(std::clamp(c.w, 0.f, 1.f) * 255.f);
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

// =============================================================================
// Inventory item data
// =============================================================================

struct InventoryItem
{
    const char* name;
    const char* stat;
    engine::math::Vec4 color;
};

static const InventoryItem kItems[] = {
    {"Iron Sword", "ATK: 25", {0.6f, 0.6f, 0.7f, 1.f}},
    {"Health Potion", "HP +50", {0.9f, 0.2f, 0.2f, 1.f}},
    {"Mana Crystal", "MP +30", {0.3f, 0.3f, 0.9f, 1.f}},
    {"Gold Shield", "DEF: 18", {0.85f, 0.75f, 0.2f, 1.f}},
    {"Fire Staff", "MAG: 35", {0.9f, 0.4f, 0.1f, 1.f}},
    {"Silver Ring", "LCK: 10", {0.8f, 0.8f, 0.85f, 1.f}},
};

static constexpr int kItemSlotCount = 16;  // 4x4 grid

// Which slots have items (slot index -> item index, -1 = empty).
static int s_slotItems[kItemSlotCount] = {
    0, 1, -1, 2, 3, -1, -1, 4, -1, -1, 5, -1, -1, -1, -1, -1,
};

// =============================================================================
// IGame lifecycle
// =============================================================================

void UiTestApp::onInit(Engine& engine, engine::ecs::Registry& /*registry*/)
{
    initVertexLayout();

    screenW_ = engine.fbWidth();
    screenH_ = engine.fbHeight();

    canvas_ = std::make_unique<UiCanvas>(screenW_, screenH_);
    uiRenderer_.init();

    // Try to load all three font backends. The bitmap path uses the same
    // embedded debug atlas as engine::ui::defaultFont() and always succeeds.
    // MSDF needs ChunkFive-msdf.{json,png} which only exist if someone ran
    // msdf-atlas-gen on the bundled ChunkFive TTF — gracefully skipped
    // otherwise. Slug needs FreeType + the ChunkFive TTF (which IS checked
    // in under assets/fonts/, SIL OFL licensed).
    // Resolve asset paths against the executable's location AND several
    // relative candidates so the app works no matter what the user's cwd
    // is when they launch ui_test. We use _NSGetExecutablePath on macOS to
    // get the binary's directory, then walk upward looking for an
    // `assets/` sibling.
    auto findAsset = [](const char* relPath) -> std::string
    {
        // 1. Cwd-relative candidates (cheap and common when running from
        //    the repo root or `cd build && ./ui_test`).
        const char* prefixes[] = {"", "../", "../../", "../../../"};
        for (const char* p : prefixes)
        {
            std::string candidate = std::string(p) + relPath;
            if (FILE* f = std::fopen(candidate.c_str(), "rb"))
            {
                std::fclose(f);
                return candidate;
            }
        }

        // 2. Walk up from the executable's directory (handles `./build/ui_test`
        //    invoked from /tmp, from $HOME, from anywhere).
        char execPath[4096] = {};
        uint32_t execPathSize = sizeof(execPath);
        if (_NSGetExecutablePath(execPath, &execPathSize) == 0)
        {
            // Strip the executable name to get the directory.
            std::string base(execPath);
            auto slash = base.find_last_of('/');
            if (slash != std::string::npos)
            {
                base.resize(slash);
            }
            // Try base/, base/.., base/../.., ... up to 5 levels.
            std::string prefix = base + "/";
            for (int depth = 0; depth < 6; ++depth)
            {
                std::string candidate = prefix + relPath;
                if (FILE* f = std::fopen(candidate.c_str(), "rb"))
                {
                    std::fclose(f);
                    return candidate;
                }
                prefix += "../";
            }
        }
        return relPath;  // give up; loadFromFile will fail with the original
    };

    std::string ttfPath = findAsset("assets/fonts/ChunkFive-Regular.ttf");
    std::string msdfJson = findAsset("assets/fonts/ChunkFive-msdf.json");
    std::string msdfPng = findAsset("assets/fonts/ChunkFive-msdf.png");

    fontLoaded_[0] = bitmapFont_.createDebugFont();
    fontLoaded_[1] = msdfFont_.loadFromFile(msdfJson.c_str(), msdfPng.c_str());
    fontLoaded_[2] = slugFont_.loadFromFile(ttfPath.c_str(), 24.f);

    std::fprintf(stderr, "[ui_test] resolved TTF path: %s\n", ttfPath.c_str());

    std::fprintf(stderr, "[ui_test] font backends loaded: Bitmap=%s MSDF=%s Slug=%s\n",
                 fontLoaded_[0] ? "yes" : "no", fontLoaded_[1] ? "yes" : "no",
                 fontLoaded_[2] ? "yes" : "no");

    // Prefer MSDF as the initial backend (sharp at any size, real font),
    // fall back to Bitmap (always loads via the embedded debug atlas), then
    // Slug. cycleFontBackend follows the same MSDF → Bitmap → Slug order.
    if (fontLoaded_[1])
    {
        currentFontIndex_ = 1;
        currentFont_ = &msdfFont_;
    }
    else if (fontLoaded_[0])
    {
        currentFontIndex_ = 0;
        currentFont_ = &bitmapFont_;
    }
    else if (fontLoaded_[2])
    {
        currentFontIndex_ = 2;
        currentFont_ = &slugFont_;
    }

    buildMainMenu();
    applyFontToCanvas();
}

void UiTestApp::onUpdate(Engine& engine, engine::ecs::Registry& /*registry*/, float dt)
{
    const auto& input = engine.inputState();

    // Update screen size on resize.
    uint16_t fbW = engine.fbWidth();
    uint16_t fbH = engine.fbHeight();
    if (fbW != screenW_ || fbH != screenH_)
    {
        screenW_ = fbW;
        screenH_ = fbH;
        canvas_->setScreenSize(screenW_, screenH_);
    }

    // Screen switching via number keys.
    if (input.isKeyPressed(Key::Num1))
    {
        switchScreen(Screen::MainMenu);
    }
    else if (input.isKeyPressed(Key::Num2))
    {
        switchScreen(Screen::Hud);
    }
    else if (input.isKeyPressed(Key::Num3))
    {
        switchScreen(Screen::Settings);
    }
    else if (input.isKeyPressed(Key::Num4))
    {
        switchScreen(Screen::Inventory);
    }

    // F = cycle font backend (MSDF → Bitmap → Slug → ...).
    if (input.isKeyPressed(Key::F))
    {
        std::fprintf(stderr, "[ui_test] F pressed: cycling from %s\n", fontBackendLabel());
        ++fontCycleCount_;
        cycleFontBackend();
        std::fprintf(stderr, "[ui_test] now active: %s\n", fontBackendLabel());
        applyFontToCanvas();
    }
    // Mouse click in the top status strip (rows 1-3 of dbgText, ~48 px tall)
    // also cycles. Gives the user a no-keyboard fallback in case F is being
    // eaten by something on their system.
    if (input.isMouseButtonPressed(engine::input::MouseButton::Left))
    {
        double mx = input.mouseX();
        double my = input.mouseY();
        if (mx >= 0 && mx < 600 && my >= 0 && my < 56)
        {
            std::fprintf(stderr, "[ui_test] status strip clicked: cycling from %s\n",
                         fontBackendLabel());
            ++fontCycleCount_;
            cycleFontBackend();
            applyFontToCanvas();
        }
    }

    // HUD-specific input.
    if (currentScreen_ == Screen::Hud)
    {
        // Space = take damage
        if (input.isKeyPressed(Key::Space))
        {
            health_ = std::max(0.f, health_ - 10.f);
        }
        // R = reload
        if (input.isKeyPressed(Key::R))
        {
            ammo_ = std::min(ammo_ + 30, maxAmmo_);
        }

        // Update HUD text/bars.
        if (healthBar_)
        {
            healthBar_->value = health_ / maxHealth_;
        }
        if (manaBar_)
        {
            manaBar_->value = mana_ / maxMana_;
        }
        if (healthLabel_)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "HP: %.0f/%.0f", health_, maxHealth_);
            healthLabel_->text = buf;
        }
        if (manaLabel_)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "MP: %.0f/%.0f", mana_, maxMana_);
            manaLabel_->text = buf;
        }
        if (scoreText_)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "Score: %d", score_);
            scoreText_->text = buf;
        }
        if (timerText_)
        {
            timer_ += dt;
            int mins = static_cast<int>(timer_) / 60;
            int secs = static_cast<int>(timer_) % 60;
            char buf[32];
            snprintf(buf, sizeof(buf), "Time: %d:%02d", mins, secs);
            timerText_->text = buf;
        }
        if (ammoText_)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "Ammo: %d/%d", ammo_, maxAmmo_);
            ammoText_->text = buf;
        }
    }

    // Settings value text updates.
    if (currentScreen_ == Screen::Settings)
    {
        char buf[16];
        if (masterValueText_)
        {
            snprintf(buf, sizeof(buf), "%.0f", masterVolume_);
            masterValueText_->text = buf;
        }
        if (musicValueText_)
        {
            snprintf(buf, sizeof(buf), "%.0f", musicVolume_);
            musicValueText_->text = buf;
        }
        if (sfxValueText_)
        {
            snprintf(buf, sizeof(buf), "%.0f", sfxVolume_);
            sfxValueText_->text = buf;
        }
        if (brightnessValueText_)
        {
            snprintf(buf, sizeof(buf), "%.0f", brightness_);
            brightnessValueText_->text = buf;
        }
    }

    // Toast update.
    updateToast(dt);

    // Dispatch mouse events to the UI canvas.
    dispatchMouseEvents(engine);

    // Update canvas (recompute layout + build draw list).
    canvas_->update();

    // Quit requested by button.
    if (requestQuit_)
    {
        glfwSetWindowShouldClose(engine.glfwHandle(), GLFW_TRUE);
    }
}

void UiTestApp::onRender(Engine& engine)
{
    renderDrawList(engine.fbWidth(), engine.fbHeight());
}

void UiTestApp::onShutdown(Engine& /*engine*/, engine::ecs::Registry& /*registry*/)
{
    uiRenderer_.shutdown();
    canvas_.reset();
}

// =============================================================================
// Screen management
// =============================================================================

void UiTestApp::switchScreen(Screen screen)
{
    // Note: we always rebuild even if the screen is unchanged, because some
    // widgets (e.g. inventory slot selection) trigger a rebuild via this path
    // to refresh their visual state.
    currentScreen_ = screen;

    // Reset canvas -- destroy all children of root, rebuild.
    canvas_ = std::make_unique<UiCanvas>(screenW_, screenH_);

    // Reset widget pointers.
    healthBar_ = nullptr;
    manaBar_ = nullptr;
    healthLabel_ = nullptr;
    manaLabel_ = nullptr;
    scoreText_ = nullptr;
    timerText_ = nullptr;
    ammoText_ = nullptr;
    masterValueText_ = nullptr;
    musicValueText_ = nullptr;
    sfxValueText_ = nullptr;
    brightnessValueText_ = nullptr;
    itemInfoText_ = nullptr;
    slotPanels_.clear();
    toastPanel_ = nullptr;
    toastText_ = nullptr;
    toastVisible_ = false;
    dialogOverlay_ = nullptr;
    dialogVisible_ = false;

    switch (screen)
    {
        case Screen::MainMenu:
            buildMainMenu();
            break;
        case Screen::Hud:
            buildHud();
            break;
        case Screen::Settings:
            buildSettings();
            break;
        case Screen::Inventory:
            buildInventory();
            break;
    }

    // Re-apply the active font to every widget the build*() functions just
    // created — they all leave font=nullptr (default), so we override here.
    applyFontToCanvas();
}

// =============================================================================
// Font backend cycling
// =============================================================================

void UiTestApp::applyFontToCanvas()
{
    if (!canvas_)
    {
        return;
    }
    // Walk the entire UiNode tree and rewrite font pointers on every text
    // widget. We rely on dynamic_cast since the canvas pool stores nodes by
    // base UiNode*.
    auto walk = [&](auto& self, engine::ui::UiNode* node) -> void
    {
        if (!node)
            return;
        if (auto* t = dynamic_cast<engine::ui::UiText*>(node))
        {
            t->font = currentFont_;
        }
        else if (auto* b = dynamic_cast<engine::ui::UiButton*>(node))
        {
            b->font = currentFont_;
        }
        for (auto* child : node->children())
        {
            self(self, child);
        }
    };
    walk(walk, canvas_->root());
}

void UiTestApp::cycleFontBackend()
{
    static const char* names[] = {"Bitmap", "MSDF", "Slug"};
    // Display order: MSDF first (most useful), then Bitmap (always works),
    // then Slug. The cycle wraps back to MSDF.
    static constexpr int kCycleOrder[3] = {1, 0, 2};

    std::fprintf(stderr, "[ui_test] cycleFontBackend: current=%d loaded=[%s,%s,%s]\n",
                 currentFontIndex_, fontLoaded_[0] ? "Y" : "N", fontLoaded_[1] ? "Y" : "N",
                 fontLoaded_[2] ? "Y" : "N");

    // Find current position in the cycle order, then advance until we land
    // on a slot that loaded successfully.
    int orderPos = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (kCycleOrder[i] == currentFontIndex_)
        {
            orderPos = i;
            break;
        }
    }

    for (int step = 1; step <= 3; ++step)
    {
        int next = kCycleOrder[(orderPos + step) % 3];
        std::fprintf(stderr, "[ui_test]   step=%d next=%d (%s) loaded=%s\n", step, next,
                     names[next], fontLoaded_[next] ? "Y" : "N");
        if (fontLoaded_[next] && next != currentFontIndex_)
        {
            currentFontIndex_ = next;
            switch (next)
            {
                case 0:
                    currentFont_ = &bitmapFont_;
                    break;
                case 1:
                    currentFont_ = &msdfFont_;
                    break;
                case 2:
                    currentFont_ = &slugFont_;
                    break;
            }
            std::fprintf(stderr, "[ui_test]   -> switched to %s\n", names[next]);
            return;
        }
    }
    std::fprintf(stderr, "[ui_test]   -> no other backend loaded; staying on %s\n",
                 names[currentFontIndex_]);
}

const char* UiTestApp::fontBackendLabel() const
{
    switch (currentFontIndex_)
    {
        case 0:
            return "Bitmap";
        case 1:
            return "MSDF";
        case 2:
            return "Slug";
        default:
            return "?";
    }
}

// =============================================================================
// Main Menu Screen
// =============================================================================

void UiTestApp::buildMainMenu()
{
    auto* root = canvas_->root();

    // Full-screen dark background panel.
    auto* bg = canvas_->createNode<UiPanel>("menu_bg");
    bg->color = {0.05f, 0.05f, 0.1f, 0.9f};
    bg->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    bg->offsetMin = {0.f, 0.f};
    bg->offsetMax = {0.f, 0.f};
    root->addChild(bg);

    // Title: "Sama UI Test" -- centered near top.
    auto* title = canvas_->createNode<UiText>("title");
    title->text = "Sama UI Test";
    title->fontSize = 48.f;
    title->color = {1.f, 1.f, 1.f, 1.f};
    title->align = TextAlign::Center;
    title->anchor = {{0.3f, 0.08f}, {0.7f, 0.15f}};
    title->offsetMin = {0.f, 0.f};
    title->offsetMax = {0.f, 0.f};
    bg->addChild(title);

    // Subtitle: "Comprehensive Widget Demo"
    auto* subtitle = canvas_->createNode<UiText>("subtitle");
    subtitle->text = "Comprehensive Widget Demo";
    subtitle->fontSize = 20.f;
    subtitle->color = {0.7f, 0.7f, 0.8f, 1.f};
    subtitle->align = TextAlign::Center;
    subtitle->anchor = {{0.3f, 0.16f}, {0.7f, 0.21f}};
    subtitle->offsetMin = {0.f, 0.f};
    subtitle->offsetMax = {0.f, 0.f};
    bg->addChild(subtitle);

    // Button container -- centered vertically.
    float btnW = 200.f;
    float btnH = 50.f;
    float btnGap = 20.f;
    float startY = 0.35f;
    float anchorLeft = 0.5f;
    float anchorRight = 0.5f;

    // "Start Game" button
    auto* btnStart = canvas_->createNode<UiButton>("btn_start");
    btnStart->label = "Start Game";
    btnStart->fontSize = 20.f;
    btnStart->normalColor = {0.15f, 0.4f, 0.15f, 1.f};
    btnStart->hoverColor = {0.2f, 0.55f, 0.2f, 1.f};
    btnStart->pressedColor = {0.1f, 0.3f, 0.1f, 1.f};
    btnStart->cornerRadius = 8.f;
    btnStart->anchor = {{anchorLeft, startY}, {anchorRight, startY}};
    btnStart->offsetMin = {-btnW / 2.f, 0.f};
    btnStart->offsetMax = {btnW / 2.f, btnH};
    btnStart->onClick = [this](UiNode& /*sender*/) { switchScreen(Screen::Hud); };
    bg->addChild(btnStart);

    // "Settings" button
    float row2Y = startY + (btnH + btnGap) / static_cast<float>(screenH_);
    auto* btnSettings = canvas_->createNode<UiButton>("btn_settings");
    btnSettings->label = "Settings";
    btnSettings->fontSize = 20.f;
    btnSettings->normalColor = {0.2f, 0.2f, 0.4f, 1.f};
    btnSettings->hoverColor = {0.3f, 0.3f, 0.55f, 1.f};
    btnSettings->pressedColor = {0.1f, 0.1f, 0.25f, 1.f};
    btnSettings->cornerRadius = 8.f;
    btnSettings->anchor = {{anchorLeft, row2Y}, {anchorRight, row2Y}};
    btnSettings->offsetMin = {-btnW / 2.f, 0.f};
    btnSettings->offsetMax = {btnW / 2.f, btnH};
    btnSettings->onClick = [this](UiNode& /*sender*/) { switchScreen(Screen::Settings); };
    bg->addChild(btnSettings);

    // "Quit" button
    float row3Y = startY + 2.f * (btnH + btnGap) / static_cast<float>(screenH_);
    auto* btnQuit = canvas_->createNode<UiButton>("btn_quit");
    btnQuit->label = "Quit";
    btnQuit->fontSize = 20.f;
    btnQuit->normalColor = {0.5f, 0.15f, 0.15f, 1.f};
    btnQuit->hoverColor = {0.65f, 0.2f, 0.2f, 1.f};
    btnQuit->pressedColor = {0.35f, 0.1f, 0.1f, 1.f};
    btnQuit->cornerRadius = 8.f;
    btnQuit->anchor = {{anchorLeft, row3Y}, {anchorRight, row3Y}};
    btnQuit->offsetMin = {-btnW / 2.f, 0.f};
    btnQuit->offsetMax = {btnW / 2.f, btnH};
    btnQuit->onClick = [this](UiNode& /*sender*/) { requestQuit_ = true; };
    bg->addChild(btnQuit);

    // Rounded-corner showcase: 5 panels at the bottom of the menu, each
    // with a different cornerRadius value. Demonstrates the rounded-rect
    // SDF shader path. Sharp (0) → tiny (4) → medium (12) → large (24) →
    // pill (48, equal to the panel's half-height).
    auto* showcaseLabel = canvas_->createNode<UiText>("showcase_label");
    showcaseLabel->text = "cornerRadius:  0px      4px      12px     24px     48px";
    showcaseLabel->fontSize = 12.f;
    showcaseLabel->color = {0.55f, 0.55f, 0.65f, 1.f};
    showcaseLabel->align = TextAlign::Center;
    showcaseLabel->anchor = {{0.05f, 0.78f}, {0.95f, 0.81f}};
    showcaseLabel->offsetMin = {0.f, 0.f};
    showcaseLabel->offsetMax = {0.f, 0.f};
    bg->addChild(showcaseLabel);

    constexpr float kRadii[] = {0.f, 4.f, 12.f, 24.f, 48.f};
    constexpr float kPanelW = 0.13f;
    constexpr float kSpacing = 0.16f;
    const float startX = 0.5f - 2.f * kSpacing;
    const engine::math::Vec4 kShowcaseColors[] = {
        {0.85f, 0.30f, 0.30f, 1.f}, {0.30f, 0.65f, 0.85f, 1.f}, {0.30f, 0.80f, 0.40f, 1.f},
        {0.85f, 0.70f, 0.20f, 1.f}, {0.65f, 0.40f, 0.85f, 1.f},
    };
    for (int i = 0; i < 5; ++i)
    {
        auto* p = canvas_->createNode<UiPanel>("showcase");
        p->color = kShowcaseColors[i];
        p->cornerRadius = kRadii[i];
        const float cx = startX + i * kSpacing;
        p->anchor = {{cx - kPanelW * 0.5f, 0.82f}, {cx + kPanelW * 0.5f, 0.88f}};
        p->offsetMin = {0.f, 0.f};
        p->offsetMax = {0.f, 0.f};
        bg->addChild(p);
    }

    // Footer text
    auto* footer = canvas_->createNode<UiText>("footer");
    footer->text = "Press 1/2/3/4 to switch screens";
    footer->fontSize = 14.f;
    footer->color = {0.5f, 0.5f, 0.6f, 1.f};
    footer->align = TextAlign::Center;
    footer->anchor = {{0.2f, 0.9f}, {0.8f, 0.95f}};
    footer->offsetMin = {0.f, 0.f};
    footer->offsetMax = {0.f, 0.f};
    bg->addChild(footer);
}

// =============================================================================
// HUD Screen
// =============================================================================

void UiTestApp::buildHud()
{
    auto* root = canvas_->root();

    // --- Top-left: health bar ---
    auto* healthPanel = canvas_->createNode<UiPanel>("health_panel");
    healthPanel->color = {0.f, 0.f, 0.f, 0.5f};
    healthPanel->anchor = {{0.f, 0.f}, {0.f, 0.f}};
    healthPanel->offsetMin = {10.f, 10.f};
    healthPanel->offsetMax = {260.f, 50.f};
    healthPanel->cornerRadius = 4.f;
    root->addChild(healthPanel);

    healthLabel_ = canvas_->createNode<UiText>("health_label");
    healthLabel_->text = "HP: 75/100";
    healthLabel_->fontSize = 14.f;
    healthLabel_->color = {1.f, 1.f, 1.f, 1.f};
    healthLabel_->anchor = {{0.f, 0.f}, {1.f, 0.4f}};
    healthLabel_->offsetMin = {8.f, 2.f};
    healthLabel_->offsetMax = {-8.f, 0.f};
    healthPanel->addChild(healthLabel_);

    healthBar_ = canvas_->createNode<UiProgressBar>("health_bar");
    healthBar_->value = health_ / maxHealth_;
    healthBar_->fillColor = {0.8f, 0.15f, 0.15f, 1.f};
    healthBar_->bgColor = {0.2f, 0.05f, 0.05f, 1.f};
    healthBar_->cornerRadius = 3.f;
    healthBar_->anchor = {{0.f, 0.5f}, {1.f, 1.f}};
    healthBar_->offsetMin = {8.f, 0.f};
    healthBar_->offsetMax = {-8.f, -4.f};
    healthPanel->addChild(healthBar_);

    // --- Top-left: mana bar (below health) ---
    auto* manaPanel = canvas_->createNode<UiPanel>("mana_panel");
    manaPanel->color = {0.f, 0.f, 0.f, 0.5f};
    manaPanel->anchor = {{0.f, 0.f}, {0.f, 0.f}};
    manaPanel->offsetMin = {10.f, 55.f};
    manaPanel->offsetMax = {260.f, 95.f};
    manaPanel->cornerRadius = 4.f;
    root->addChild(manaPanel);

    manaLabel_ = canvas_->createNode<UiText>("mana_label");
    manaLabel_->text = "MP: 50/100";
    manaLabel_->fontSize = 14.f;
    manaLabel_->color = {1.f, 1.f, 1.f, 1.f};
    manaLabel_->anchor = {{0.f, 0.f}, {1.f, 0.4f}};
    manaLabel_->offsetMin = {8.f, 2.f};
    manaLabel_->offsetMax = {-8.f, 0.f};
    manaPanel->addChild(manaLabel_);

    manaBar_ = canvas_->createNode<UiProgressBar>("mana_bar");
    manaBar_->value = mana_ / maxMana_;
    manaBar_->fillColor = {0.15f, 0.3f, 0.85f, 1.f};
    manaBar_->bgColor = {0.05f, 0.05f, 0.2f, 1.f};
    manaBar_->cornerRadius = 3.f;
    manaBar_->anchor = {{0.f, 0.5f}, {1.f, 1.f}};
    manaBar_->offsetMin = {8.f, 0.f};
    manaBar_->offsetMax = {-8.f, -4.f};
    manaPanel->addChild(manaBar_);

    // --- Top-right: score ---
    scoreText_ = canvas_->createNode<UiText>("score");
    scoreText_->text = "Score: 12,450";
    scoreText_->fontSize = 20.f;
    scoreText_->color = {1.f, 0.95f, 0.3f, 1.f};
    scoreText_->align = TextAlign::Right;
    scoreText_->anchor = {{1.f, 0.f}, {1.f, 0.f}};
    scoreText_->offsetMin = {-200.f, 15.f};
    scoreText_->offsetMax = {-10.f, 40.f};
    root->addChild(scoreText_);

    // --- Top-right: timer (below score) ---
    timerText_ = canvas_->createNode<UiText>("timer");
    timerText_->text = "Time: 2:34";
    timerText_->fontSize = 16.f;
    timerText_->color = {0.8f, 0.8f, 0.8f, 1.f};
    timerText_->align = TextAlign::Right;
    timerText_->anchor = {{1.f, 0.f}, {1.f, 0.f}};
    timerText_->offsetMin = {-200.f, 45.f};
    timerText_->offsetMax = {-10.f, 65.f};
    root->addChild(timerText_);

    // --- Bottom-center: ammo counter ---
    auto* ammoPanel = canvas_->createNode<UiPanel>("ammo_panel");
    ammoPanel->color = {0.f, 0.f, 0.f, 0.6f};
    ammoPanel->cornerRadius = 4.f;
    ammoPanel->anchor = {{0.5f, 1.f}, {0.5f, 1.f}};
    ammoPanel->offsetMin = {-80.f, -45.f};
    ammoPanel->offsetMax = {80.f, -10.f};
    root->addChild(ammoPanel);

    ammoText_ = canvas_->createNode<UiText>("ammo_text");
    ammoText_->text = "Ammo: 30/120";
    ammoText_->fontSize = 18.f;
    ammoText_->color = {1.f, 1.f, 1.f, 1.f};
    ammoText_->align = TextAlign::Center;
    ammoText_->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    ammoText_->offsetMin = {4.f, 4.f};
    ammoText_->offsetMax = {-4.f, -4.f};
    ammoPanel->addChild(ammoText_);

    // --- Bottom-left: minimap placeholder ---
    auto* minimap = canvas_->createNode<UiPanel>("minimap");
    minimap->color = {0.15f, 0.15f, 0.2f, 0.8f};
    minimap->cornerRadius = 4.f;
    minimap->anchor = {{0.f, 1.f}, {0.f, 1.f}};
    minimap->offsetMin = {10.f, -140.f};
    minimap->offsetMax = {140.f, -10.f};
    root->addChild(minimap);

    auto* mapLabel = canvas_->createNode<UiText>("map_label");
    mapLabel->text = "MAP";
    mapLabel->fontSize = 18.f;
    mapLabel->color = {0.5f, 0.5f, 0.6f, 1.f};
    mapLabel->align = TextAlign::Center;
    mapLabel->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    mapLabel->offsetMin = {0.f, 0.f};
    mapLabel->offsetMax = {0.f, 0.f};
    minimap->addChild(mapLabel);

    // --- Center: crosshair ---
    auto* crosshair = canvas_->createNode<UiPanel>("crosshair");
    crosshair->color = {1.f, 1.f, 1.f, 0.8f};
    crosshair->anchor = {{0.5f, 0.5f}, {0.5f, 0.5f}};
    crosshair->offsetMin = {-2.f, -2.f};
    crosshair->offsetMax = {2.f, 2.f};
    crosshair->interactable = false;
    root->addChild(crosshair);

    // --- Controls hint ---
    auto* hint = canvas_->createNode<UiText>("hud_hint");
    hint->text = "Space=damage  R=reload  1/2/3/4=screens";
    hint->fontSize = 12.f;
    hint->color = {0.5f, 0.5f, 0.5f, 0.7f};
    hint->align = TextAlign::Center;
    hint->anchor = {{0.2f, 1.f}, {0.8f, 1.f}};
    hint->offsetMin = {0.f, -60.f};
    hint->offsetMax = {0.f, -48.f};
    root->addChild(hint);
}

// =============================================================================
// Settings Screen
// =============================================================================

void UiTestApp::buildSettings()
{
    auto* root = canvas_->root();

    // Background
    auto* bg = canvas_->createNode<UiPanel>("settings_bg");
    bg->color = {0.08f, 0.08f, 0.12f, 0.95f};
    bg->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    bg->offsetMin = {0.f, 0.f};
    bg->offsetMax = {0.f, 0.f};
    root->addChild(bg);

    // Title
    auto* title = canvas_->createNode<UiText>("settings_title");
    title->text = "Settings";
    title->fontSize = 36.f;
    title->color = {1.f, 1.f, 1.f, 1.f};
    title->align = TextAlign::Center;
    title->anchor = {{0.3f, 0.05f}, {0.7f, 0.12f}};
    title->offsetMin = {0.f, 0.f};
    title->offsetMax = {0.f, 0.f};
    bg->addChild(title);

    // Center panel
    auto* panel = canvas_->createNode<UiPanel>("settings_panel");
    panel->color = {0.12f, 0.12f, 0.18f, 1.f};
    panel->cornerRadius = 8.f;
    panel->anchor = {{0.2f, 0.15f}, {0.8f, 0.85f}};
    panel->offsetMin = {0.f, 0.f};
    panel->offsetMax = {0.f, 0.f};
    bg->addChild(panel);

    // Helper lambda for slider rows.
    float rowH = 60.f;
    float panelH = static_cast<float>(screenH_) * 0.7f;  // approx panel height
    int rowIndex = 0;

    auto addSliderRow = [&](const char* labelStr, float* valuePtr,
                            UiText** valueTxtOut) -> UiSlider*
    {
        float rowTop = (rowIndex * rowH) / panelH;
        float rowBot = ((rowIndex + 1) * rowH) / panelH;

        // Label
        auto* lbl = canvas_->createNode<UiText>(labelStr);
        lbl->text = labelStr;
        lbl->fontSize = 16.f;
        lbl->color = {0.85f, 0.85f, 0.9f, 1.f};
        lbl->anchor = {{0.05f, rowTop}, {0.3f, rowBot}};
        lbl->offsetMin = {0.f, 15.f};
        lbl->offsetMax = {0.f, -15.f};
        panel->addChild(lbl);

        // Slider
        auto* slider = canvas_->createNode<UiSlider>(labelStr);
        slider->value = *valuePtr / 100.f;
        slider->fillColor = {0.3f, 0.6f, 1.f, 1.f};
        slider->trackColor = {0.25f, 0.25f, 0.3f, 1.f};
        slider->thumbColor = {1.f, 1.f, 1.f, 1.f};
        slider->trackHeight = 6.f;
        slider->thumbSize = 18.f;
        slider->anchor = {{0.32f, rowTop}, {0.8f, rowBot}};
        slider->offsetMin = {0.f, 18.f};
        slider->offsetMax = {0.f, -18.f};
        slider->onValueChanged = [valuePtr](UiSlider& /*s*/, float v) { *valuePtr = v * 100.f; };
        panel->addChild(slider);

        // Value text
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", *valuePtr);
        auto* valTxt = canvas_->createNode<UiText>("val");
        valTxt->text = buf;
        valTxt->fontSize = 16.f;
        valTxt->color = {1.f, 1.f, 1.f, 1.f};
        valTxt->align = TextAlign::Center;
        valTxt->anchor = {{0.82f, rowTop}, {0.95f, rowBot}};
        valTxt->offsetMin = {0.f, 15.f};
        valTxt->offsetMax = {0.f, -15.f};
        panel->addChild(valTxt);

        *valueTxtOut = valTxt;
        rowIndex++;
        return slider;
    };

    addSliderRow("Master Volume", &masterVolume_, &masterValueText_);
    addSliderRow("Music Volume", &musicVolume_, &musicValueText_);
    addSliderRow("SFX Volume", &sfxVolume_, &sfxValueText_);
    addSliderRow("Brightness", &brightness_, &brightnessValueText_);

    // --- Difficulty row ---
    {
        float rowTop = (rowIndex * rowH) / panelH;
        float rowBot = ((rowIndex + 1) * rowH) / panelH;

        auto* diffLabel = canvas_->createNode<UiText>("diff_label");
        diffLabel->text = "Difficulty";
        diffLabel->fontSize = 16.f;
        diffLabel->color = {0.85f, 0.85f, 0.9f, 1.f};
        diffLabel->anchor = {{0.05f, rowTop}, {0.3f, rowBot}};
        diffLabel->offsetMin = {0.f, 15.f};
        diffLabel->offsetMax = {0.f, -15.f};
        panel->addChild(diffLabel);

        const char* diffNames[] = {"Easy", "Normal", "Hard"};
        engine::math::Vec4 diffColors[] = {
            {0.2f, 0.6f, 0.2f, 1.f},
            {0.5f, 0.5f, 0.2f, 1.f},
            {0.7f, 0.2f, 0.2f, 1.f},
        };

        for (int i = 0; i < 3; ++i)
        {
            float btnLeft = 0.35f + i * 0.15f;
            float btnRight = btnLeft + 0.12f;

            auto* btn = canvas_->createNode<UiButton>(diffNames[i]);
            btn->label = diffNames[i];
            btn->fontSize = 14.f;
            btn->cornerRadius = 6.f;
            btn->anchor = {{btnLeft, rowTop}, {btnRight, rowBot}};
            btn->offsetMin = {2.f, 15.f};
            btn->offsetMax = {-2.f, -15.f};

            if (i == difficulty_)
            {
                btn->normalColor = diffColors[i];
                btn->hoverColor = diffColors[i] + engine::math::Vec4(0.1f, 0.1f, 0.1f, 0.f);
            }
            else
            {
                btn->normalColor = {0.2f, 0.2f, 0.25f, 1.f};
                btn->hoverColor = {0.3f, 0.3f, 0.35f, 1.f};
            }
            btn->pressedColor = diffColors[i] - engine::math::Vec4(0.05f, 0.05f, 0.05f, 0.f);

            int diffIdx = i;
            btn->onClick = [this, diffIdx](UiNode& /*sender*/)
            {
                difficulty_ = diffIdx;
                switchScreen(Screen::Settings);  // rebuild to update toggle state
            };
            panel->addChild(btn);
        }
        rowIndex++;
    }

    // --- Fullscreen toggle row ---
    {
        float rowTop = (rowIndex * rowH) / panelH;
        float rowBot = ((rowIndex + 1) * rowH) / panelH;

        auto* fsLabel = canvas_->createNode<UiText>("fs_label");
        fsLabel->text = "Fullscreen";
        fsLabel->fontSize = 16.f;
        fsLabel->color = {0.85f, 0.85f, 0.9f, 1.f};
        fsLabel->anchor = {{0.05f, rowTop}, {0.3f, rowBot}};
        fsLabel->offsetMin = {0.f, 15.f};
        fsLabel->offsetMax = {0.f, -15.f};
        panel->addChild(fsLabel);

        auto* fsBtn = canvas_->createNode<UiButton>("fs_toggle");
        fsBtn->label = fullscreen_ ? "ON" : "OFF";
        fsBtn->fontSize = 14.f;
        fsBtn->cornerRadius = 6.f;
        fsBtn->anchor = {{0.35f, rowTop}, {0.5f, rowBot}};
        fsBtn->offsetMin = {2.f, 15.f};
        fsBtn->offsetMax = {-2.f, -15.f};

        if (fullscreen_)
        {
            fsBtn->normalColor = {0.2f, 0.6f, 0.3f, 1.f};
            fsBtn->hoverColor = {0.3f, 0.7f, 0.4f, 1.f};
        }
        else
        {
            fsBtn->normalColor = {0.3f, 0.2f, 0.2f, 1.f};
            fsBtn->hoverColor = {0.4f, 0.3f, 0.3f, 1.f};
        }
        fsBtn->onClick = [this](UiNode& /*sender*/)
        {
            fullscreen_ = !fullscreen_;
            switchScreen(Screen::Settings);  // rebuild
        };
        panel->addChild(fsBtn);
        rowIndex++;
    }

    // --- Back button ---
    auto* backBtn = canvas_->createNode<UiButton>("back_btn");
    backBtn->label = "Back";
    backBtn->fontSize = 18.f;
    backBtn->normalColor = {0.25f, 0.25f, 0.35f, 1.f};
    backBtn->hoverColor = {0.35f, 0.35f, 0.5f, 1.f};
    backBtn->pressedColor = {0.15f, 0.15f, 0.2f, 1.f};
    backBtn->cornerRadius = 8.f;
    backBtn->anchor = {{0.5f, 0.88f}, {0.5f, 0.88f}};
    backBtn->offsetMin = {-80.f, 0.f};
    backBtn->offsetMax = {80.f, 45.f};
    backBtn->onClick = [this](UiNode& /*sender*/) { switchScreen(Screen::MainMenu); };
    bg->addChild(backBtn);

    // Footer
    auto* footer = canvas_->createNode<UiText>("settings_footer");
    footer->text = "Press 1/2/3/4 to switch screens";
    footer->fontSize = 12.f;
    footer->color = {0.4f, 0.4f, 0.5f, 0.7f};
    footer->align = TextAlign::Center;
    footer->anchor = {{0.2f, 0.94f}, {0.8f, 0.98f}};
    footer->offsetMin = {0.f, 0.f};
    footer->offsetMax = {0.f, 0.f};
    bg->addChild(footer);
}

// =============================================================================
// Inventory Screen
// =============================================================================

void UiTestApp::buildInventory()
{
    auto* root = canvas_->root();

    // Background
    auto* bg = canvas_->createNode<UiPanel>("inv_bg");
    bg->color = {0.06f, 0.06f, 0.1f, 0.95f};
    bg->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    bg->offsetMin = {0.f, 0.f};
    bg->offsetMax = {0.f, 0.f};
    root->addChild(bg);

    // Title
    auto* title = canvas_->createNode<UiText>("inv_title");
    title->text = "Inventory";
    title->fontSize = 32.f;
    title->color = {1.f, 1.f, 1.f, 1.f};
    title->align = TextAlign::Center;
    title->anchor = {{0.3f, 0.05f}, {0.7f, 0.12f}};
    title->offsetMin = {0.f, 0.f};
    title->offsetMax = {0.f, 0.f};
    bg->addChild(title);

    // Grid container
    auto* gridPanel = canvas_->createNode<UiPanel>("grid_panel");
    gridPanel->color = {0.1f, 0.1f, 0.15f, 0.8f};
    gridPanel->cornerRadius = 8.f;
    gridPanel->anchor = {{0.25f, 0.15f}, {0.75f, 0.72f}};
    gridPanel->offsetMin = {0.f, 0.f};
    gridPanel->offsetMax = {0.f, 0.f};
    bg->addChild(gridPanel);

    // 4x4 grid of item slots
    slotPanels_.clear();
    float cellSize = 1.f / 4.f;

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            int slotIdx = row * 4 + col;
            float ax0 = col * cellSize;
            float ay0 = row * cellSize;
            float ax1 = (col + 1) * cellSize;
            float ay1 = (row + 1) * cellSize;

            // Slot background (button for click handling).
            char slotName[32];
            snprintf(slotName, sizeof(slotName), "slot_%d", slotIdx);

            auto* slotBtn = canvas_->createNode<UiButton>(slotName);
            slotBtn->label = "";
            slotBtn->cornerRadius = 4.f;
            slotBtn->anchor = {{ax0, ay0}, {ax1, ay1}};
            slotBtn->offsetMin = {4.f, 4.f};
            slotBtn->offsetMax = {-4.f, -4.f};

            bool isSelected = (slotIdx == selectedSlot_);
            if (isSelected)
            {
                slotBtn->normalColor = {0.3f, 0.3f, 0.5f, 1.f};
                slotBtn->hoverColor = {0.35f, 0.35f, 0.55f, 1.f};
            }
            else
            {
                slotBtn->normalColor = {0.15f, 0.15f, 0.2f, 1.f};
                slotBtn->hoverColor = {0.2f, 0.2f, 0.3f, 1.f};
            }
            slotBtn->pressedColor = {0.25f, 0.25f, 0.4f, 1.f};

            int capturedSlot = slotIdx;
            slotBtn->onClick = [this, capturedSlot](UiNode& /*sender*/)
            {
                selectedSlot_ = capturedSlot;
                switchScreen(Screen::Inventory);  // rebuild for highlight
            };
            gridPanel->addChild(slotBtn);

            // If slot has an item, add a colored panel inside.
            int itemIdx = s_slotItems[slotIdx];
            if (itemIdx >= 0)
            {
                auto* itemPanel = canvas_->createNode<UiPanel>("item");
                itemPanel->color = kItems[itemIdx].color;
                itemPanel->cornerRadius = 4.f;
                itemPanel->anchor = {{0.15f, 0.15f}, {0.85f, 0.85f}};
                itemPanel->offsetMin = {0.f, 0.f};
                itemPanel->offsetMax = {0.f, 0.f};
                itemPanel->interactable = false;
                slotBtn->addChild(itemPanel);
            }

            slotPanels_.push_back(nullptr);  // track if needed
        }
    }

    // Item info panel (below grid)
    auto* infoPanel = canvas_->createNode<UiPanel>("info_panel");
    infoPanel->color = {0.1f, 0.1f, 0.15f, 0.8f};
    infoPanel->cornerRadius = 6.f;
    infoPanel->anchor = {{0.25f, 0.75f}, {0.75f, 0.85f}};
    infoPanel->offsetMin = {0.f, 0.f};
    infoPanel->offsetMax = {0.f, 0.f};
    bg->addChild(infoPanel);

    itemInfoText_ = canvas_->createNode<UiText>("item_info");
    if (selectedSlot_ >= 0 && selectedSlot_ < kItemSlotCount && s_slotItems[selectedSlot_] >= 0)
    {
        int idx = s_slotItems[selectedSlot_];
        char buf[64];
        snprintf(buf, sizeof(buf), "%s  -  %s", kItems[idx].name, kItems[idx].stat);
        itemInfoText_->text = buf;
    }
    else
    {
        itemInfoText_->text = "Select an item slot";
    }
    itemInfoText_->fontSize = 18.f;
    itemInfoText_->color = {0.9f, 0.9f, 0.95f, 1.f};
    itemInfoText_->align = TextAlign::Center;
    itemInfoText_->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    itemInfoText_->offsetMin = {10.f, 5.f};
    itemInfoText_->offsetMax = {-10.f, -5.f};
    infoPanel->addChild(itemInfoText_);

    // Toast demo button
    auto* toastBtn = canvas_->createNode<UiButton>("toast_btn");
    toastBtn->label = "Show Toast";
    toastBtn->fontSize = 14.f;
    toastBtn->normalColor = {0.3f, 0.2f, 0.5f, 1.f};
    toastBtn->hoverColor = {0.4f, 0.3f, 0.6f, 1.f};
    toastBtn->cornerRadius = 6.f;
    toastBtn->anchor = {{0.25f, 0.87f}, {0.45f, 0.87f}};
    toastBtn->offsetMin = {0.f, 0.f};
    toastBtn->offsetMax = {0.f, 40.f};
    toastBtn->onClick = [this](UiNode& /*sender*/) { showToast("Achievement Unlocked!"); };
    bg->addChild(toastBtn);

    // Dialog demo button
    auto* dialogBtn = canvas_->createNode<UiButton>("dialog_btn");
    dialogBtn->label = "Show Dialog";
    dialogBtn->fontSize = 14.f;
    dialogBtn->normalColor = {0.5f, 0.3f, 0.2f, 1.f};
    dialogBtn->hoverColor = {0.6f, 0.4f, 0.3f, 1.f};
    dialogBtn->cornerRadius = 6.f;
    dialogBtn->anchor = {{0.55f, 0.87f}, {0.75f, 0.87f}};
    dialogBtn->offsetMin = {0.f, 0.f};
    dialogBtn->offsetMax = {0.f, 40.f};
    dialogBtn->onClick = [this](UiNode& /*sender*/) { showDialog("Are you sure?"); };
    bg->addChild(dialogBtn);

    // Footer
    auto* footer = canvas_->createNode<UiText>("inv_footer");
    footer->text = "Press 1/2/3/4 to switch screens";
    footer->fontSize = 12.f;
    footer->color = {0.4f, 0.4f, 0.5f, 0.7f};
    footer->align = TextAlign::Center;
    footer->anchor = {{0.2f, 0.95f}, {0.8f, 0.99f}};
    footer->offsetMin = {0.f, 0.f};
    footer->offsetMax = {0.f, 0.f};
    bg->addChild(footer);
}

// =============================================================================
// Toast system
// =============================================================================

void UiTestApp::showToast(const std::string& message)
{
    if (toastPanel_)
    {
        // Remove existing toast from canvas by hiding it.
        toastPanel_->visible = false;
    }

    // Create toast panel at top of screen.
    toastPanel_ = canvas_->createNode<UiPanel>("toast_panel");
    toastPanel_->color = {0.15f, 0.5f, 0.15f, 0.9f};
    toastPanel_->cornerRadius = 6.f;
    toastPanel_->anchor = {{0.3f, 0.f}, {0.7f, 0.f}};
    toastPanel_->offsetMin = {0.f, 10.f};
    toastPanel_->offsetMax = {0.f, 50.f};
    toastPanel_->interactable = false;
    canvas_->root()->addChild(toastPanel_);

    toastText_ = canvas_->createNode<UiText>("toast_text");
    toastText_->text = message;
    toastText_->fontSize = 16.f;
    toastText_->color = {1.f, 1.f, 1.f, 1.f};
    toastText_->align = TextAlign::Center;
    toastText_->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    toastText_->offsetMin = {5.f, 5.f};
    toastText_->offsetMax = {-5.f, -5.f};
    toastPanel_->addChild(toastText_);

    toastTimer_ = 3.f;
    toastVisible_ = true;
}

void UiTestApp::updateToast(float dt)
{
    if (!toastVisible_ || !toastPanel_)
    {
        return;
    }

    toastTimer_ -= dt;
    if (toastTimer_ <= 0.f)
    {
        toastPanel_->visible = false;
        toastVisible_ = false;
    }
    else if (toastTimer_ < 1.f)
    {
        // Fade out during last second.
        toastPanel_->opacity = toastTimer_;
        if (toastText_)
        {
            toastText_->color.w = toastTimer_;
        }
        // Also fade the panel color alpha.
        toastPanel_->color.w = 0.9f * toastTimer_;
    }
}

// =============================================================================
// Dialog system
// =============================================================================

void UiTestApp::showDialog(const std::string& message)
{
    if (dialogVisible_)
    {
        return;
    }

    // Semi-transparent overlay.
    dialogOverlay_ = canvas_->createNode<UiPanel>("dialog_overlay");
    dialogOverlay_->color = {0.f, 0.f, 0.f, 0.5f};
    dialogOverlay_->anchor = {{0.f, 0.f}, {1.f, 1.f}};
    dialogOverlay_->offsetMin = {0.f, 0.f};
    dialogOverlay_->offsetMax = {0.f, 0.f};
    canvas_->root()->addChild(dialogOverlay_);

    // Dialog box.
    auto* dialogBox = canvas_->createNode<UiPanel>("dialog_box");
    dialogBox->color = {0.15f, 0.15f, 0.22f, 1.f};
    dialogBox->cornerRadius = 10.f;
    dialogBox->anchor = {{0.5f, 0.5f}, {0.5f, 0.5f}};
    dialogBox->offsetMin = {-160.f, -80.f};
    dialogBox->offsetMax = {160.f, 80.f};
    dialogOverlay_->addChild(dialogBox);

    // Message text.
    auto* msgText = canvas_->createNode<UiText>("dialog_msg");
    msgText->text = message;
    msgText->fontSize = 22.f;
    msgText->color = {1.f, 1.f, 1.f, 1.f};
    msgText->align = TextAlign::Center;
    msgText->anchor = {{0.f, 0.1f}, {1.f, 0.5f}};
    msgText->offsetMin = {10.f, 0.f};
    msgText->offsetMax = {-10.f, 0.f};
    dialogBox->addChild(msgText);

    // Yes button.
    auto* yesBtn = canvas_->createNode<UiButton>("dialog_yes");
    yesBtn->label = "Yes";
    yesBtn->fontSize = 16.f;
    yesBtn->normalColor = {0.2f, 0.5f, 0.2f, 1.f};
    yesBtn->hoverColor = {0.3f, 0.65f, 0.3f, 1.f};
    yesBtn->pressedColor = {0.15f, 0.35f, 0.15f, 1.f};
    yesBtn->cornerRadius = 6.f;
    yesBtn->anchor = {{0.1f, 0.6f}, {0.45f, 0.85f}};
    yesBtn->offsetMin = {5.f, 5.f};
    yesBtn->offsetMax = {-5.f, -5.f};
    yesBtn->onClick = [this](UiNode& /*sender*/)
    {
        hideDialog();
        showToast("Confirmed!");
    };
    dialogBox->addChild(yesBtn);

    // No button.
    auto* noBtn = canvas_->createNode<UiButton>("dialog_no");
    noBtn->label = "No";
    noBtn->fontSize = 16.f;
    noBtn->normalColor = {0.5f, 0.2f, 0.2f, 1.f};
    noBtn->hoverColor = {0.65f, 0.3f, 0.3f, 1.f};
    noBtn->pressedColor = {0.35f, 0.15f, 0.15f, 1.f};
    noBtn->cornerRadius = 6.f;
    noBtn->anchor = {{0.55f, 0.6f}, {0.9f, 0.85f}};
    noBtn->offsetMin = {5.f, 5.f};
    noBtn->offsetMax = {-5.f, -5.f};
    noBtn->onClick = [this](UiNode& /*sender*/)
    {
        hideDialog();
        showToast("Cancelled.");
    };
    dialogBox->addChild(noBtn);

    dialogVisible_ = true;
}

void UiTestApp::hideDialog()
{
    if (dialogOverlay_)
    {
        dialogOverlay_->visible = false;
    }
    dialogVisible_ = false;
}

// =============================================================================
// Mouse event dispatch
// =============================================================================

void UiTestApp::dispatchMouseEvents(Engine& engine)
{
    const auto& input = engine.inputState();
    float scaleX = engine.contentScaleX();
    float scaleY = engine.contentScaleY();
    float mx = static_cast<float>(input.mouseX()) * scaleX;
    float my = static_cast<float>(input.mouseY()) * scaleY;
    bool mouseDown = input.isMouseButtonHeld(MouseButton::Left);

    // Mouse move.
    if (mx != prevMouseX_ || my != prevMouseY_)
    {
        UiEvent moveEvt;
        moveEvt.type = UiEventType::MouseMove;
        moveEvt.position = {mx, my};
        moveEvt.button = 0;
        canvas_->dispatchEvent(moveEvt);
    }

    // Mouse down.
    if (input.isMouseButtonPressed(MouseButton::Left))
    {
        UiEvent downEvt;
        downEvt.type = UiEventType::MouseDown;
        downEvt.position = {mx, my};
        downEvt.button = 0;
        canvas_->dispatchEvent(downEvt);
    }

    // Mouse up.
    if (input.isMouseButtonReleased(MouseButton::Left))
    {
        UiEvent upEvt;
        upEvt.type = UiEventType::MouseUp;
        upEvt.position = {mx, my};
        upEvt.button = 0;
        canvas_->dispatchEvent(upEvt);
    }

    prevMouseX_ = mx;
    prevMouseY_ = my;
    prevMouseDown_ = mouseDown;
}

// =============================================================================
// Render draw list using bgfx
// =============================================================================

void UiTestApp::renderDrawList(uint16_t fbW, uint16_t fbH)
{
    // Set up the UI view: clear + viewport. UiRenderer handles the
    // orthographic projection and submits all rect commands using the
    // sprite shader program.
    bgfx::ViewId viewId = kViewUi;
    bgfx::setViewName(viewId, "UI");
    bgfx::setViewRect(viewId, 0, 0, fbW, fbH);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1A1A2EFF, 1.0f, 0);
    bgfx::touch(viewId);

    // Status header — always rendered via bgfx debug text so it's visible
    // regardless of which font backend is currently active. The dbgText
    // overlay is owned by bgfx itself; its readability does NOT depend on
    // the IFont path working. This lets the user verify that F cycling is
    // actually happening even when the selected backend (e.g. Slug) hasn't
    // wired up its UiRenderer integration yet.
    bgfx::dbgTextClear();
    static const char* screenNames[] = {"Main Menu", "HUD", "Settings", "Inventory"};
    bgfx::dbgTextPrintf(1, 1, 0x0f, "UI Test - %s   [Font: %s]",
                        screenNames[static_cast<int>(currentScreen_)], fontBackendLabel());
    bgfx::dbgTextPrintf(1, 2, 0x07, "Loaded: Bitmap=%s  MSDF=%s  Slug=%s    F or click here=cycle",
                        fontLoaded_[0] ? "yes" : "no", fontLoaded_[1] ? "yes" : "no",
                        fontLoaded_[2] ? "yes" : "no");
    bgfx::dbgTextPrintf(1, 3, 0x0a, "Cycle counter: %d (increments on every F press)",
                        fontCycleCount_);

    // Sample line — rendered via drawText with the currently selected font.
    // If the backend can render text properly, this line appears. If it
    // can't (e.g. Slug pre-integration), the dbgText status above still
    // tells the user which backend is active.
    char sampleBuf[128];
    std::snprintf(sampleBuf, sizeof(sampleBuf), "Sample (%s): The quick brown fox jumps 0123",
                  fontBackendLabel());
    canvas_->drawList().drawText({12.f, 50.f}, sampleBuf, {1.f, 1.f, 1.f, 1.f}, currentFont_, 18.f);

    // UiRenderer walks rect + text commands and submits batched draw calls
    // using the engine's default bitmap font for any Text command whose
    // `font` pointer is null.
    uiRenderer_.render(canvas_->drawList(), viewId, fbW, fbH);
}
