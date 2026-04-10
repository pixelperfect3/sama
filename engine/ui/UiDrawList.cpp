#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

void UiDrawList::drawRect(math::Vec2 pos, math::Vec2 size, math::Vec4 color, float cornerRadius)
{
    UiDrawCmd cmd{};
    cmd.type = UiDrawCmd::Rect;
    cmd.position = pos;
    cmd.size = size;
    cmd.color = color;
    cmd.cornerRadius = cornerRadius;
    commands_.push_back(cmd);
}

void UiDrawList::drawTexturedRect(math::Vec2 pos, math::Vec2 size, bgfx::TextureHandle tex,
                                  math::Vec4 uv, math::Vec4 tint)
{
    UiDrawCmd cmd{};
    cmd.type = UiDrawCmd::TexturedRect;
    cmd.position = pos;
    cmd.size = size;
    cmd.color = tint;
    cmd.texture = tex;
    cmd.uvRect = uv;
    commands_.push_back(cmd);
}

void UiDrawList::drawText(math::Vec2 pos, const char* text, math::Vec4 color, const IFont* font,
                          float fontSize)
{
    UiDrawCmd cmd{};
    cmd.type = UiDrawCmd::Text;
    cmd.position = pos;
    cmd.size = {0.f, 0.f};
    cmd.color = color;
    cmd.text = text;
    cmd.font = font;
    cmd.fontSize = fontSize;
    commands_.push_back(cmd);
}

}  // namespace engine::ui
