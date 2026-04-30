#pragma once

#include <cstdint>

#include "engine/rendering/HandleTypes.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// bgfx view ID layout — fixed constants used by every render system.
//
// Layout (see NOTES.md "Implementation Decisions"):
//
//   0 – 7   Shadow maps    kViewShadowBase + cascade/spot index.
//                          Up to 8 shadow views (4 CSM cascades + 4 spot lights).
//   8       Depth prepass  IMR/desktop only. Skipped on TBDR (isTBDR=true).
//   9       Opaque pass    Forward+ PBR.
//   10      Transparent    Back-to-front, same light clusters.
//  11–13    Reserved       Future passes (e.g. decals, velocity).
//  14–15    UI / HUD       14 = 3D sprites, 15 = ImGui/editor overlay.
//  16–47    Post-process   kViewPostProcessBase. Bloom alone needs 10+ views;
//                          each sub-pass gets its own ID in this range.
//
// bgfx default BGFX_CONFIG_MAX_VIEWS = 256 — plenty of headroom.
// Never hardcode numeric view IDs anywhere outside this header.
// ---------------------------------------------------------------------------

// Guard against bgfx ever changing its underlying ViewId type.  If this
// fires, update HandleTypes.h's ViewId alias to match (and double-check
// that nothing in the engine assumed a specific width).
static_assert(sizeof(ViewId) == sizeof(uint16_t), "ViewId must remain 16-bit");

// Shadow pass range — one view per cascade / spot light.
inline constexpr ViewId kViewShadowBase = 0;
inline constexpr ViewId kMaxShadowViews = 8;

// Fixed scene passes.
inline constexpr ViewId kViewDepth = 8;
inline constexpr ViewId kViewOpaque = 9;
inline constexpr ViewId kViewTransparent = 10;

// UI / HUD.
inline constexpr ViewId kViewUiBase = 14;
inline constexpr ViewId kViewUi = 14;     // 3D sprites / world-space UI
inline constexpr ViewId kViewImGui = 15;  // editor overlay

// Post-process sub-pass range (bloom, SSAO, tonemap, FXAA, ...).
// PostProcessSystem allocates IDs sequentially from this base each frame.
inline constexpr ViewId kViewPostProcessBase = 16;
inline constexpr ViewId kMaxPostProcessViews = 32;  // views 16-47

// Game UI — rendered after all post-processing so bloom/FXAA/tonemapping
// do not affect text and icons.  Uses orthographic projection.
inline constexpr ViewId kViewGameUi = 48;

// Debug HUD — on top of everything, for debug/status text overlays.
inline constexpr ViewId kViewDebugHud = 49;

}  // namespace engine::rendering
