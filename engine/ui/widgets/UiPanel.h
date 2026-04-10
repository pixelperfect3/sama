#pragma once

#include "engine/ui/UiNode.h"

namespace engine::ui
{

// Container with optional background color and rounded corners.
class UiPanel : public UiNode
{
public:
    math::Vec4 color{0.1f, 0.1f, 0.1f, 1.0f};
    math::Vec4 borderColor{0.f, 0.f, 0.f, 0.f};
    float borderWidth = 0.f;
    float cornerRadius = 0.f;

protected:
    void onDraw(UiDrawList& drawList) const override;
};

}  // namespace engine::ui
