#pragma once

#include <bgfx/bgfx.h>

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// bgfx view IDs — one per render pass, fixed constants.
// Order determines pass execution order within bgfx's internal submission.
// ---------------------------------------------------------------------------

inline constexpr bgfx::ViewId kViewShadow = 0;
inline constexpr bgfx::ViewId kViewDepth = 1;
inline constexpr bgfx::ViewId kViewOpaque = 2;
inline constexpr bgfx::ViewId kViewTransparent = 3;
inline constexpr bgfx::ViewId kViewPostProcess = 4;
inline constexpr bgfx::ViewId kViewUi = 5;

}  // namespace engine::rendering
