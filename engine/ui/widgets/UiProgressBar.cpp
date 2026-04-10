#include "engine/ui/widgets/UiProgressBar.h"

#include <algorithm>

#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

void UiProgressBar::onDraw(UiDrawList& drawList) const
{
    const auto& r = rect();

    // Draw background.
    drawList.drawRect(r.position, r.size, bgColor, cornerRadius);

    // Draw filled portion.
    float clampedValue = std::clamp(value, 0.f, 1.f);
    float fillWidth = r.size.x * clampedValue;
    if (fillWidth > 0.f)
    {
        drawList.drawRect(r.position, {fillWidth, r.size.y}, fillColor, cornerRadius);
    }
}

}  // namespace engine::ui
