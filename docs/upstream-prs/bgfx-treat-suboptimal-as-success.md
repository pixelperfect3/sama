# renderer_vk: treat VK_SUBOPTIMAL_KHR as success, not as out-of-date

> Draft PR for https://github.com/bkaradzic/bgfx — review before submitting.
> Verified absent in master as of 2026-06-18 (case blocks below still group
> SUBOPTIMAL with OUT_OF_DATE at `src/renderer_vk.cpp:8359-8362` and
> `:8444-8447`).

## Problem

`bgfx`'s Vulkan backend treats `VK_SUBOPTIMAL_KHR` identically to
`VK_ERROR_OUT_OF_DATE_KHR` in both `SwapChainVK::acquire` and
`SwapChainVK::present`: both flip `m_needToRecreateSwapchain = true`, which
forces a `vkDeviceWaitIdle` + `vkDestroySwapchainKHR` + `vkCreateSwapchainKHR`
+ image-view recreation on the very next frame.

This is conservative-by-spec but catastrophic on devices where SUBOPTIMAL is
*permanent*.

### Concrete repro

Pixel 9 / Android 16 (Tensor G4, Mali-G715) returns `VK_SUBOPTIMAL_KHR` from
every `vkQueuePresentKHR` for the entire activity lifetime.  The root cause is
the display cutout: `dumpsys SurfaceFlinger` reports
`displayCutoutSafeInsets=Rect(173, 0 - 0, 0)`, so the activity's drawable
region is 2251×1080, but the `VkSurfaceCapabilitiesKHR.currentExtent` reports
the full 2424×1080.  The application creates the swapchain at 2251×1080
(driver clamp of the requested 2424×1080).  The compositor flags this as
suboptimal against the surface's preferred extent and continues to do so on
every present — rebuilding the swapchain doesn't change the clamp behaviour;
the new swapchain is also 2251×1080 and the next present returns SUBOPTIMAL
again.

Observed effect: ~650 `vkCreateSwapchainKHR` calls over 12 seconds, frame
time pinned at ~16 ms regardless of GPU load.  Per-frame BX_TRACE confirms
`vkQueuePresentKHR(...): result = VK_SUBOPTIMAL_KHR` repeating every 15-17 ms,
followed immediately by `Create swapchain numSwapChainImages 5, ...`.

### Why VK_SUBOPTIMAL_KHR is not an error

Per the Vulkan spec (VkSwapchainKHR(3), Vulkan Guide § Swapchains):

> `VK_SUBOPTIMAL_KHR` ... the swapchain no longer matches the surface
> properties exactly, but **can still be used to successfully present to the
> surface**.

Applications are explicitly allowed to keep using a swapchain that returned
SUBOPTIMAL.  Recreation is *optional*.  Most production engines do not
recreate: Unreal, Unity, Godot all ignore SUBOPTIMAL for the present-result
path and only recreate on `VK_ERROR_OUT_OF_DATE_KHR`.

## Fix

Split the `case VK_SUBOPTIMAL_KHR` from `case VK_ERROR_OUT_OF_DATE_KHR` in
both call sites.  Treat SUBOPTIMAL as success; emit a one-shot trace so the
behaviour is auditable from logcat without flooding it.

```diff
--- a/src/renderer_vk.cpp
+++ b/src/renderer_vk.cpp
@@ -157,6 +157,11 @@ VK_IMPORT_DEVICE
 		{ VK_PRESENT_MODE_IMMEDIATE_KHR,    false, "VK_PRESENT_MODE_IMMEDIATE_KHR"    },
 	};
 
+	// One-shot trace gate for the SUBOPTIMAL-as-success path.  Some
+	// surfaces (Android display-cutout devices) report SUBOPTIMAL every
+	// frame; we want to know once it triggered, not 60 times a second.
+	static bool s_suboptimalLogged = false;
+
 #define VK_IMPORT_FUNC(_optional, _func) PFN_##_func _func
 
 VK_IMPORT
@@ -8356,9 +8361,21 @@ VK_IMPORT_DEVICE
 				return false;
 
 			case VK_ERROR_OUT_OF_DATE_KHR:
-			case VK_SUBOPTIMAL_KHR:
 				m_needToRecreateSwapchain = true;
 				return false;
+
+			case VK_SUBOPTIMAL_KHR:
+				// VK_SUBOPTIMAL_KHR is a soft hint per the spec — the
+				// image WAS acquired, the surface just has a preferred
+				// config the swapchain doesn't match.  Some surfaces
+				// (cutout-bearing Android displays) report this every
+				// frame; recreating the swapchain doesn't clear the
+				// flag and burns ~15 ms per frame.  Treat as success.
+				if (!s_suboptimalLogged)
+				{
+					BX_TRACE("vkAcquireNextImageKHR returned VK_SUBOPTIMAL_KHR — ignoring (presentable)");
+					s_suboptimalLogged = true;
+				}
+				break;
 
 			default:
 				BX_ASSERT(VK_SUCCESS == result, "vkAcquireNextImageKHR(...); VK error 0x%x: %s", result, getName(result) );
@@ -8441,9 +8458,18 @@ VK_IMPORT_DEVICE
 				break;
 
 			case VK_ERROR_OUT_OF_DATE_KHR:
-			case VK_SUBOPTIMAL_KHR:
 				m_needToRecreateSwapchain = true;
 				break;
+
+			case VK_SUBOPTIMAL_KHR:
+				// See SwapChainVK::acquire above.  The frame WAS
+				// presented; the swapchain just isn't the surface's
+				// preferred config.  Recreating doesn't help if the
+				// mismatch is permanent (display cutout, etc.).
+				if (!s_suboptimalLogged)
+				{
+					BX_TRACE("vkQueuePresentKHR returned VK_SUBOPTIMAL_KHR — ignoring (presented)");
+					s_suboptimalLogged = true;
+				}
+				break;
 
 			default:
 				BX_ASSERT(VK_SUCCESS == result, "vkQueuePresentKHR(...); VK error 0x%x: %s", result, getName(result) );
```

The shared static `s_suboptimalLogged` covers both call sites with a single
log line per process lifetime.  Reset of the flag on swapchain recreation is
intentionally absent — a recreation that *clears* the SUBOPTIMAL condition
would simply not re-trigger it; one that doesn't is exactly the loop we're
avoiding.

## Alternatives considered

**Add a `BGFX_RESET_TOLERATE_SUBOPTIMAL` opt-in flag instead of changing
default behaviour.**  Preserves bug-for-bug compatibility for existing users
who may rely on the recreation behaviour.  Downsides: (a) the recreation
behaviour is broken on cutout devices regardless — users hitting that path
will have to find the flag, (b) every other major engine ignores SUBOPTIMAL by
default, so users coming from those engines will hit the bug, (c) silent
"my GPU usage is 100% and FPS is at vsync" is a worst-case failure mode for a
new flag.  Open to changing direction here if maintainer prefers opt-in.

**Add a `BGFX_RESET_RECREATE_ON_SUBOPTIMAL` opt-out flag layered on the new
default.**  Best of both worlds — change default to ignore (matching the
ecosystem and fixing the cutout case), expose a flag for the rare user who
actually wants the old behaviour.  Happy to add this if maintainer wants the
escape hatch.

**Don't change bgfx at all; require applications to call
`bgfx::reset` with the actually-clamped extent.**  Doesn't work — the
application doesn't know what the driver will clamp to until after the first
swapchain creation, and on cutout devices the clamp is conditional on
unrelated state (status-bar visibility, IME open/close) so it can change
mid-session.

## Test plan

Tested locally on:
- macOS 14.3 / Apple M2 Max — `engine_tests` 26981 assertions across 753
  test cases pass.  No SUBOPTIMAL observed (Metal backend, but the renderer_vk
  path is still compiled).
- Linux x86_64 / Vulkan / RADV (Mesa 24) — full app suite runs, no SUBOPTIMAL
  observed.
- Pixel 9 / Android 16 / Vulkan / Mali-G715 — **the repro device**.  Before
  this patch: `bgfx::frameMs` ~16 ms pinned to vsync floor, ~650
  `vkCreateSwapchainKHR` per 12 s.  After: `bgfx::frameMs` 0.27–6.27 ms, one
  `vkCreateSwapchainKHR` per process lifetime (at init), panel auto-promoted
  by Choreographer to 120 Hz, FPS ~336 uncapped, one `vkQueuePresentKHR
  returned VK_SUBOPTIMAL_KHR — ignoring (presented)` log line at startup,
  silent thereafter.

Suggested additional test from maintainer side: run the bgfx examples on a
device with a display cutout (any Pixel 6+, recent Galaxy, OnePlus 11+) and
confirm no per-frame swapchain recreate in `dumpsys SurfaceFlinger
--latency-clear` output.

## Note about prior art in this repo

There are two recent commits that touch the same area (`1d8f6d8` and
`edbec2f`, both 2026-06-17, "Vulkan: do not block acquiring window swap chain
images" / "Vulkan: recreate window swap chains flagged for recreation").
Neither addresses SUBOPTIMAL handling specifically — they refactor the *when*
of recreation, not the *whether*.  This PR composes cleanly on top of both at
the time of writing.

## Companion fix

The integrating team also hit a second self-sustaining recreate loop on the
same device, independent of SUBOPTIMAL — see the
[shadow-requested-resolution PR draft](./bgfx-shadow-requested-resolution.md).
Both fixes are required to close the Pixel 9 case; either alone leaves the
other loop active.  Worth landing as two separate PRs for bisect / review
clarity but the team will recommend both together.
