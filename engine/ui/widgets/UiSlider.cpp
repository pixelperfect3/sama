#include "engine/ui/widgets/UiSlider.h"

#include <algorithm>

#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

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
