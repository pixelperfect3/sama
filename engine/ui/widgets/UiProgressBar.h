#pragma once

#include "engine/ui/UiNode.h"

namespace engine::ui
{

// Filled rectangle showing progress from 0 to 1.
class UiProgressBar : public UiNode
{
public:
    float value = 0.f;  // clamped to [0, 1]

    math::Vec4 bgColor{0.2f, 0.2f, 0.2f, 1.0f};
    math::Vec4 fillColor{0.2f, 0.8f, 0.2f, 1.0f};
    float cornerRadius = 0.f;

protected:
    void onDraw(UiDrawList& drawList) const override;
};

}  // namespace engine::ui
