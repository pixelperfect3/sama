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
//  11       Sel-stencil    Editor only. Stencil-write pass for selection
//                          outline. Targets HDR scene fb's D24S8 attachment.
//  12       Sel-outline    Editor only. Inflated silhouette draw, gated by
//                          stencil_test=NOT_EQUAL 1. Targets HDR scene fb.
//  13–15    Reserved       Future scene passes (decals, velocity, particles).
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

// Editor-only scene-pass views — must run BEFORE the post-process tonemap
// (view 16) because they write into the HDR scene FB and rely on its
// D24S8 stencil attachment.  Runtime engine never submits to these.
//   11  Selection-stencil  Renders the selected mesh with stencil-write
//                          state (REF=1, OP_PASS=REPLACE).  Color writes off,
//                          depth test on so only the visible surface marks
//                          stencil = 1.
//   12  Selection-outline  Re-renders the selected mesh inflated along its
//                          oct-decoded normal with stencil_test = NOT_EQUAL 1.
//                          Depth test off so the outline shows through any
//                          geometry that occludes the selected entity (the
//                          whole point of the feature: "where is my selection
//                          when the gizmo is hidden?").
inline constexpr ViewId kViewEditorSelectionStencil = 11;
inline constexpr ViewId kViewEditorSelectionOutline = 12;

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

// Editor-only overlay views (above the engine-shared range above).
//
//   52  Gizmo  Editor transform-gizmo lines / arrows.
//              Owned by editor/gizmo/GizmoRenderer.h (kept there as a class
//              constant for backwards compatibility, but documented here
//              too).
inline constexpr ViewId kViewEditorGizmo = 52;

}  // namespace engine::rendering
