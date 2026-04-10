#pragma once

#include <functional>

#include "engine/ui/UiEvent.h"
#include "engine/ui/UiNode.h"

namespace engine::ui
{

// Horizontal slider with a value in [0, 1] and onValueChanged callback.
class UiSlider : public UiNode
{
public:
    using ValueChangedCallback = std::function<void(UiSlider& sender, float newValue)>;
    ValueChangedCallback onValueChanged;

    float value = 0.f;        // clamped to [0, 1]
    float trackHeight = 4.f;  // logical pixels
    float thumbSize = 16.f;   // logical pixels

    // Style
    math::Vec4 trackColor{0.3f, 0.3f, 0.3f, 1.0f};
    math::Vec4 fillColor{0.2f, 0.6f, 1.0f, 1.0f};
    math::Vec4 thumbColor{1.f, 1.f, 1.f, 1.0f};

    bool onEvent(const UiEvent& event) override;

protected:
    void onDraw(UiDrawList& drawList) const override;

private:
    bool dragging_ = false;
};

}  // namespace engine::ui
