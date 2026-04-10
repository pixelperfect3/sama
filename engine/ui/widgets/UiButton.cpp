#include "engine/ui/widgets/UiButton.h"

#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiEvent.h"

namespace engine::ui
{

bool UiButton::onEvent(const UiEvent& event)
{
    switch (event.type)
    {
        case UiEventType::MouseDown:
            if (event.button == 0)
            {
                state_ = State::Pressed;
                if (onClick)
                {
                    onClick(*this);
                }
                return true;
            }
            break;
        case UiEventType::MouseUp:
            if (event.button == 0 && state_ == State::Pressed)
            {
                state_ = State::Hovered;
                return true;
            }
            break;
        case UiEventType::MouseEnter:
            if (state_ != State::Pressed)
            {
                state_ = State::Hovered;
            }
            if (onHover)
            {
                onHover(*this);
            }
            return false;
        case UiEventType::MouseExit:
            state_ = State::Normal;
            return false;
        default:
            break;
    }
    return false;
}

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
        // Approximate text dimensions using bgfx debug font (8x16 px/char).
        constexpr float kCharW = 8.f;
        constexpr float kCharH = 16.f;
        const float textW = static_cast<float>(label.size()) * kCharW;
        const float textH = kCharH;

        math::Vec2 textPos;
        textPos.x = r.position.x + (r.size.x - textW) * 0.5f;
        textPos.y = r.position.y + (r.size.y - textH) * 0.5f;
        drawList.drawText(textPos, label.c_str(), textColor);
    }
}

}  // namespace engine::ui
