#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/game/IGame.h"
#include "engine/math/Types.h"
#include "engine/ui/BitmapFont.h"
#include "engine/ui/DebugHud.h"
#include "engine/ui/MsdfFont.h"
#include "engine/ui/SlugFont.h"
#include "engine/ui/UiCanvas.h"
#include "engine/ui/UiRenderer.h"

namespace engine::ui
{
class IFont;
class UiPanel;
class UiText;
class UiButton;
class UiSlider;
class UiProgressBar;
}  // namespace engine::ui

// =============================================================================
// UiTestApp -- comprehensive UI test demonstrating all widget types.
//
// Screens:
//   1 — Main Menu      (title, subtitle, buttons)
//   2 — HUD            (health/mana bars, score, timer, minimap, crosshair)
//   3 — Settings       (sliders, toggle buttons)
//   4 — Inventory      (4x4 item grid, selection, info)
//
// Press 1/2/3/4 to switch screens.
// Press F to cycle the active font backend (Bitmap → MSDF → Slug → ...).
// Backends that fail to load (missing asset, FreeType disabled) are skipped.
// =============================================================================

class UiTestApp final : public engine::game::IGame
{
public:
    UiTestApp() = default;

    void onInit(engine::core::Engine& engine, engine::ecs::Registry& registry) override;
    void onUpdate(engine::core::Engine& engine, engine::ecs::Registry& registry, float dt) override;
    void onRender(engine::core::Engine& engine) override;
    void onShutdown(engine::core::Engine& engine, engine::ecs::Registry& registry) override;

private:
    // Screen enum
    enum class Screen : uint8_t
    {
        MainMenu = 0,
        Hud,
        Settings,
        Inventory
    };

    void switchScreen(Screen screen);

    // Screen builders -- each populates canvas_ from scratch.
    void buildMainMenu();
    void buildHud();
    void buildSettings();
    void buildInventory();

    // Render the UiDrawList using bgfx transient buffers + dbgText.
    void renderDrawList(uint16_t fbW, uint16_t fbH);

    // Toast system
    void showToast(const std::string& message);
    void updateToast(float dt);

    // Dialog system
    void showDialog(const std::string& message);
    void hideDialog();

    // Helpers
    void dispatchMouseEvents(engine::core::Engine& engine);

    // Font backend cycling. Walks the canvas tree and rewrites every
    // UiText/UiButton's font pointer to `currentFont_`. Called from
    // switchScreen() and from the F-key handler.
    void applyFontToCanvas();
    // Pick the next backend that loaded successfully (skips failed ones).
    void cycleFontBackend();
    // Human-readable label for the active backend, used in the title bar.
    const char* fontBackendLabel() const;

    // State
    Screen currentScreen_ = Screen::MainMenu;
    std::unique_ptr<engine::ui::UiCanvas> canvas_;
    engine::ui::UiRenderer uiRenderer_;
    uint16_t screenW_ = 1280;
    uint16_t screenH_ = 720;

    // Font backends. Loaded once in onInit; pointers stay valid for the
    // lifetime of the app. The bitmap backend always loads (it uses an
    // embedded debug atlas with no filesystem dependency). MSDF and Slug
    // may fail if assets or FreeType are missing — fontLoaded_[i] tracks
    // which slots are usable.
    engine::ui::BitmapFont bitmapFont_;
    engine::ui::MsdfFont msdfFont_;
    engine::ui::SlugFont slugFont_;
    bool fontLoaded_[3] = {false, false, false};  // [Bitmap, MSDF, Slug]
    int currentFontIndex_ = 0;                    // 0=Bitmap, 1=MSDF, 2=Slug
    int fontCycleCount_ = 0;                      // diagnostic: # of F presses
    engine::ui::IFont* currentFont_ = nullptr;

    // HUD state
    float health_ = 75.f;
    float maxHealth_ = 100.f;
    float mana_ = 50.f;
    float maxMana_ = 100.f;
    int score_ = 12450;
    float timer_ = 154.f;  // 2:34 in seconds
    int ammo_ = 30;
    int maxAmmo_ = 120;
    engine::ui::UiProgressBar* healthBar_ = nullptr;
    engine::ui::UiProgressBar* manaBar_ = nullptr;
    engine::ui::UiText* healthLabel_ = nullptr;
    engine::ui::UiText* manaLabel_ = nullptr;
    engine::ui::UiText* scoreText_ = nullptr;
    engine::ui::UiText* timerText_ = nullptr;
    engine::ui::UiText* ammoText_ = nullptr;

    // Settings state
    float masterVolume_ = 75.f;
    float musicVolume_ = 50.f;
    float sfxVolume_ = 80.f;
    float brightness_ = 60.f;
    int difficulty_ = 1;  // 0=Easy, 1=Normal, 2=Hard
    bool fullscreen_ = false;
    engine::ui::UiText* masterValueText_ = nullptr;
    engine::ui::UiText* musicValueText_ = nullptr;
    engine::ui::UiText* sfxValueText_ = nullptr;
    engine::ui::UiText* brightnessValueText_ = nullptr;

    // Inventory state
    int selectedSlot_ = -1;
    engine::ui::UiText* itemInfoText_ = nullptr;
    std::vector<engine::ui::UiPanel*> slotPanels_;

    // Toast state
    engine::ui::UiPanel* toastPanel_ = nullptr;
    engine::ui::UiText* toastText_ = nullptr;
    float toastTimer_ = 0.f;
    bool toastVisible_ = false;

    // Dialog state
    engine::ui::UiPanel* dialogOverlay_ = nullptr;
    bool dialogVisible_ = false;

    // Debug HUD
    engine::ui::DebugHud hud_;

    // Mouse tracking for event dispatch
    double prevMouseX_ = 0.0;
    double prevMouseY_ = 0.0;
    bool prevMouseDown_ = false;
    bool requestQuit_ = false;
};
