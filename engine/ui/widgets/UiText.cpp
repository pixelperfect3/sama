#include "engine/ui/widgets/UiText.h"

#include "engine/ui/DefaultFont.h"
#include "engine/ui/IFont.h"
#include "engine/ui/Measure.h"
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

    // Measure via the (possibly-default) font so alignment matches what
    // the UiRenderer will actually emit.
    const IFont* useFont = font ? font : defaultFont();
    const math::Vec2 size = measureText(useFont, fontSize, text.c_str());

    // Compute aligned position within the node rect.
    math::Vec2 pos = r.position;
    pos.y += (r.size.y - size.y) * 0.5f;  // vertical center always
    switch (align)
    {
        case TextAlign::Left:
            pos.x += 4.f;  // small left padding
            break;
        case TextAlign::Center:
            pos.x += (r.size.x - size.x) * 0.5f;
            break;
        case TextAlign::Right:
            pos.x += r.size.x - size.x - 4.f;
            break;
    }

    drawList.drawText(pos, text.c_str(), color, font, fontSize);
}

}  // namespace engine::ui
