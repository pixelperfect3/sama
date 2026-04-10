#pragma once

#include <cstdint>
#include <string>

#include "engine/ui/UiNode.h"

namespace engine::ui
{

class IFont;

// Text alignment within the node rect.
enum class TextAlign : uint8_t
{
    Left,
    Center,
    Right
};

// Displays a text string with font reference, color, and alignment.
class UiText : public UiNode
{
public:
    std::string text;

    // Non-owning font pointer. When null, onDraw falls back to the engine
    // default font both for measurement and for emitting the draw command,
    // so existing widgets keep working without explicit wiring.
    const IFont* font = nullptr;

    uint32_t fontId = 0;  // reserved for future asset lookup
    float fontSize = 16.f;
    math::Vec4 color{1.f, 1.f, 1.f, 1.f};
    TextAlign align = TextAlign::Left;

protected:
    void onDraw(UiDrawList& drawList) const override;
};

}  // namespace engine::ui
