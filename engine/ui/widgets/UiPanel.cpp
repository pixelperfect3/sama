#include "engine/ui/widgets/UiPanel.h"

#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

void UiPanel::onDraw(UiDrawList& drawList) const
{
    const auto& r = rect();
    drawList.drawRect(r.position, r.size, color, cornerRadius);

    if (borderWidth > 0.f && borderColor.w > 0.f)
    {
        // Draw border as a slightly larger rect behind the fill.
        // This is a simplified approach; a proper border would use
        // outline geometry or a shader.
        drawList.drawRect({r.position.x - borderWidth, r.position.y - borderWidth},
                          {r.size.x + borderWidth * 2.f, r.size.y + borderWidth * 2.f}, borderColor,
                          cornerRadius);
    }
}

}  // namespace engine::ui
