#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

#include "engine/math/Types.h"

namespace engine::ui
{

class IFont;  // forward decl — see engine/ui/IFont.h

struct UiDrawCmd
{
    enum Type : uint8_t
    {
        Rect,
        TexturedRect,
        Text
    };

    Type type = Rect;
    math::Vec2 position{0.f, 0.f};
    math::Vec2 size{0.f, 0.f};
    math::Vec4 color{1.f, 1.f, 1.f, 1.f};  // RGBA
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    math::Vec4 uvRect{0.f, 0.f, 1.f, 1.f};
    const char* text = nullptr;  // for Text type
    float cornerRadius = 0.f;

    // Text-only payload. Both fields are nullable; the renderer falls back
    // to the global default font when `font == nullptr`.
    const IFont* font = nullptr;
    float fontSize = 16.f;
};

class UiDrawList
{
public:
    void drawRect(math::Vec2 pos, math::Vec2 size, math::Vec4 color, float cornerRadius = 0.f);

    void drawTexturedRect(math::Vec2 pos, math::Vec2 size, bgfx::TextureHandle tex,
                          math::Vec4 uv = {0.f, 0.f, 1.f, 1.f},
                          math::Vec4 tint = {1.f, 1.f, 1.f, 1.f});

    // Records a Text command. The renderer walks `text` glyph-by-glyph and
    // emits one quad per glyph using the font's atlas + program. If `font`
    // is null the renderer substitutes the engine's default font.
    void drawText(math::Vec2 pos, const char* text, math::Vec4 color, const IFont* font = nullptr,
                  float fontSize = 16.f);

    const std::vector<UiDrawCmd>& commands() const noexcept
    {
        return commands_;
    }
    void clear()
    {
        commands_.clear();
    }

private:
    std::vector<UiDrawCmd> commands_;
};

}  // namespace engine::ui
