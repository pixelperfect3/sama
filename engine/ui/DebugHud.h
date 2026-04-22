#pragma once

#include <cstdint>

namespace engine::ui
{

// ---------------------------------------------------------------------------
// DebugHud — lightweight HUD for debug/status text.
//
// Replaces direct bgfx::dbgTextPrintf() usage in game code.
// Uses UiRenderer + BitmapFont internally, works on all platforms
// including Android (where bgfx debug text is unavailable).
//
// Usage:
//   DebugHud hud;
//   hud.init();
//   // each frame:
//   hud.begin(fbWidth, fbHeight);
//   hud.printf(0, 0, "FPS: %.1f", fps);
//   hud.printf(0, 1, "Entities: %d", count);
//   hud.end();
//
// Coordinates are in character cells (column, row), matching
// bgfx::dbgTextPrintf conventions. Each cell is 8x16 pixels.
// ---------------------------------------------------------------------------

class IFont;
class UiDrawList;
class UiRenderer;

class DebugHud
{
public:
    DebugHud();
    ~DebugHud();

    DebugHud(const DebugHud&) = delete;
    DebugHud& operator=(const DebugHud&) = delete;

    void init();
    void shutdown();

    // Begin a new frame of debug text. Call once per frame before printf().
    void begin(uint32_t fbWidth, uint32_t fbHeight);

    // Print formatted text at character cell (column, row).
    // Supports standard printf format specifiers.
    // color is packed RGBA (default 0xFFFFFFFF = opaque white).
    void printf(uint16_t col, uint16_t row, uint32_t color, const char* fmt, ...);
    void printf(uint16_t col, uint16_t row, const char* fmt, ...);

    // Render all accumulated text. Call once per frame after all printf() calls.
    void end();

private:
    UiRenderer* renderer_ = nullptr;
    UiDrawList* drawList_ = nullptr;
    IFont* font_ = nullptr;
    uint32_t fbWidth_ = 0;
    uint32_t fbHeight_ = 0;
};

}  // namespace engine::ui
