#include "engine/ui/widgets/UiText.h"

#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

void UiText::onDraw(UiDrawList& drawList) const
{
    if (text.empty())
    {
        return;
    }

    const auto& r = rect();

    // Approximate text dimensions using bgfx debug font metrics (8x16 px/char).
    // This will be replaced with real font metrics when bitmap fonts ship.
    constexpr float kCharW = 8.f;
    constexpr float kCharH = 16.f;
    const float textW = static_cast<float>(text.size()) * kCharW;
    const float textH = kCharH;

    // Compute aligned position within the node rect.
    math::Vec2 pos = r.position;
    pos.y += (r.size.y - textH) * 0.5f;  // vertical center always
    switch (align)
    {
        case TextAlign::Left:
            pos.x += 4.f;  // small left padding
            break;
        case TextAlign::Center:
            pos.x += (r.size.x - textW) * 0.5f;
            break;
        case TextAlign::Right:
            pos.x += r.size.x - textW - 4.f;
            break;
    }

    drawList.drawText(pos, text.c_str(), color);
}

}  // namespace engine::ui
