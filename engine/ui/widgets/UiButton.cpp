#include "engine/ui/widgets/UiButton.h"

#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

void UiButton::onDraw(UiDrawList& drawList) const
{
    const auto& r = rect();

    // Select color based on current state.
    math::Vec4 bgColor = normalColor;
    switch (state_)
    {
        case State::Hovered:
            bgColor = hoverColor;
            break;
        case State::Pressed:
            bgColor = pressedColor;
            break;
        default:
            break;
    }

    // Draw background.
    drawList.drawRect(r.position, r.size, bgColor, cornerRadius);

    // Draw label text centered in the button.
    if (!label.empty())
    {
        drawList.drawText(r.position, label.c_str(), textColor);
    }
}

}  // namespace engine::ui
