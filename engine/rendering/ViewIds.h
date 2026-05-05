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
//   9       Opaque pass    Forward+ PBR (HDR scene framebuffer).
//   10      Transparent    Back-to-front, same light clusters (HDR fb).
//  11–15    Reserved       Future scene passes (decals, velocity, particles).
//  16–47    Post-process   kViewPostProcessBase. SSAO/bloom/tonemap/FXAA;
//                          each sub-pass gets its own ID.  The final pass
//                          writes the LDR backbuffer.
//  48–51    UI / HUD       All UI is intentionally rendered AFTER post-process
//                          so tonemap/bloom/FXAA do not touch text and icons.
//                          Pre-Phase 7 layouts had ImGui at 15 / sprites at 14;
//                          those got clobbered by the post-process FXAA write.
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

// Post-process sub-pass range (bloom, SSAO, tonemap, FXAA, ...).
// PostProcessSystem allocates IDs sequentially from this base each frame.
inline constexpr ViewId kViewPostProcessBase = 16;
inline constexpr ViewId kMaxPostProcessViews = 32;  // views 16-47

// UI / HUD — all rendered after post-process so tonemap/bloom/FXAA do not
// touch text and icons.  Uses orthographic projection.
inline constexpr ViewId kViewGameUi = 48;
inline constexpr ViewId kViewDebugHud = 49;
inline constexpr ViewId kViewImGui = 50;  // ImGui / editor overlay
inline constexpr ViewId kViewUi = 51;     // 3D sprites / world-space UI
inline constexpr ViewId kViewUiBase = kViewGameUi;

}  // namespace engine::rendering
