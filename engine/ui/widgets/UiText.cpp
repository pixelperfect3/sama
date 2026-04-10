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

    // For now, emit a single text draw command at the node's position.
    // Alignment and font metrics will be resolved when the text renderer
    // is implemented.
    drawList.drawText(r.position, text.c_str(), color);
}

}  // namespace engine::ui
