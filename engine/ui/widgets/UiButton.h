#pragma once

#include <cstdint>
#include <string>

#include "engine/ui/UiEvent.h"
#include "engine/ui/UiNode.h"

namespace engine::ui
{

class IFont;

// Clickable panel with a text label and onClick callback.
class UiButton : public UiNode
{
public:
    UiCallback onClick;
    UiCallback onHover;

    std::string label;
    const IFont* font = nullptr;  // null -> default font
    uint32_t fontId = 0;
    float fontSize = 16.f;

    // Style
    math::Vec4 normalColor{0.2f, 0.2f, 0.2f, 1.0f};
    math::Vec4 hoverColor{0.3f, 0.3f, 0.4f, 1.0f};
    math::Vec4 pressedColor{0.1f, 0.1f, 0.15f, 1.0f};
    math::Vec4 textColor{1.f, 1.f, 1.f, 1.f};
    float cornerRadius = 4.f;

    bool onEvent(const UiEvent& event) override;

protected:
    void onDraw(UiDrawList& drawList) const override;

private:
    enum class State : uint8_t
    {
        Normal,
        Hovered,
        Pressed
    };
    State state_ = State::Normal;
};

}  // namespace engine::ui
