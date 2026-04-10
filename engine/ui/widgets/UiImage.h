#pragma once

#include <bgfx/bgfx.h>

#include "engine/ui/UiNode.h"

namespace engine::ui
{

// Displays a texture (bgfx::TextureHandle + UV rect).
class UiImage : public UiNode
{
public:
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    math::Vec4 uvRect{0.f, 0.f, 1.f, 1.f};
    math::Vec4 tint{1.f, 1.f, 1.f, 1.f};
    bool preserveAspect = false;

protected:
    void onDraw(UiDrawList& drawList) const override;
};

}  // namespace engine::ui
