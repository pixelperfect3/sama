#include "engine/ui/widgets/UiImage.h"

#include "engine/ui/UiDrawList.h"

namespace engine::ui
{

void UiImage::onDraw(UiDrawList& drawList) const
{
    const auto& r = rect();

    if (bgfx::isValid(texture))
    {
        drawList.drawTexturedRect(r.position, r.size, texture, uvRect, tint);
    }
}

}  // namespace engine::ui
