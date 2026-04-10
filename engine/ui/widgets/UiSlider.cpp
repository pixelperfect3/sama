#include "engine/ui/widgets/UiSlider.h"

#include <algorithm>

#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiEvent.h"

namespace engine::ui
{

bool UiSlider::onEvent(const UiEvent& event)
{
    const auto& r = rect();

    auto computeValue = [&]() -> float
    {
        float relative = event.position.x - r.position.x;
        return std::clamp(relative / r.size.x, 0.f, 1.f);
    };

    switch (event.type)
    {
        case UiEventType::MouseDown:
            if (event.button == 0)
            {
                dragging_ = true;
                float newValue = computeValue();
                if (newValue != value)
                {
                    value = newValue;
                    if (onValueChanged)
                    {
                        onValueChanged(*this, value);
                    }
                }
                return true;
            }
            break;
        case UiEventType::MouseMove:
            if (dragging_)
            {
                float newValue = computeValue();
                if (newValue != value)
                {
                    value = newValue;
                    if (onValueChanged)
                    {
                        onValueChanged(*this, value);
                    }
                }
                return true;
            }
            break;
        case UiEventType::MouseUp:
            if (event.button == 0 && dragging_)
            {
                dragging_ = false;
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

void UiSlider::onDraw(UiDrawList& drawList) const
{
    const auto& r = rect();

    // Draw track (centered vertically within the node rect).
    float trackY = r.position.y + (r.size.y - trackHeight) * 0.5f;
    drawList.drawRect({r.position.x, trackY}, {r.size.x, trackHeight}, trackColor);

    // Draw filled portion.
    float clampedValue = std::clamp(value, 0.f, 1.f);
    float fillWidth = r.size.x * clampedValue;
    drawList.drawRect({r.position.x, trackY}, {fillWidth, trackHeight}, fillColor);

    // Draw thumb.
    float thumbX = r.position.x + fillWidth - thumbSize * 0.5f;
    float thumbY = r.position.y + (r.size.y - thumbSize) * 0.5f;
    drawList.drawRect({thumbX, thumbY}, {thumbSize, thumbSize}, thumbColor);
}

}  // namespace engine::ui
