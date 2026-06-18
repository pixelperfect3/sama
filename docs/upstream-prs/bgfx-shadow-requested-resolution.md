# renderer_vk: shadow last-requested resolution so driver clamp doesn't loop

> Draft PR for https://github.com/bkaradzic/bgfx — review before submitting.
> Verified absent in master as of 2026-06-18 (comparison at
> `src/renderer_vk.cpp:3082-3090` still uses `m_resolution.width/height`
> directly; clamp-overwrite at `:3108-3111` unchanged).

## Problem

In `RendererContextVK::updateResolution()`, two different uses of
`m_resolution.width/height` collide:

```cpp
// src/renderer_vk.cpp ~3082
if (false
||  m_resolution.formatColor        != _resolution.formatColor
||  m_resolution.formatDepthStencil != _resolution.formatDepthStencil
||  m_resolution.width              != _resolution.width   // (1)
||  m_resolution.height             != _resolution.height  // (1)
||  m_resolution.reset              != flags
||  m_backBuffer.m_swapChain.m_needToRecreateSurface
||  m_backBuffer.m_swapChain.m_needToRecreateSwapchain)
{
    ...
    m_resolution = _resolution;
    m_resolution.reset = flags;
    ...
    m_backBuffer.update(m_commandBuffer, m_resolution);
    // Update the resolution again here, as the actual width and height
    // is now final (as it was potentially clamped by the Vulkan driver).
    m_resolution.width = m_backBuffer.m_width;     // (2)
    m_resolution.height = m_backBuffer.m_height;   // (2)
    ...
}
```

`(1)` uses `m_resolution.width/height` for the comparison "did the application
ask for a new size?"  `(2)` overwrites those same fields with the *clamped*
swapchain extent that the driver actually granted (the comment at `:3107`
explicitly documents this is intentional — downstream bgfx code reads
`m_resolution.width/height` for viewport sizing and needs the actual swapchain
extent, not the requested one).

The collision: when the driver clamps, `(2)` writes the clamped value, and
`(1)` next frame compares that clamped value against the still-unchanged
application request, sees a mismatch, and triggers another recreate.  The new
swapchain is created at the same request → driver clamps to the same
clamped value → `(1)` sees the same mismatch next frame → infinite loop.

### Concrete repro

Pixel 9 / Android 16 (Mali-G715), with a display cutout:

- App calls `bgfx::reset(2424, 1080, ...)` (the full panel width from
  `ANativeWindow_getWidth`).
- Driver clamps the swapchain to 2424→2251 (the cutout-safe extent from
  `VkSurfaceCapabilitiesKHR.maxImageExtent.width`).
- `(2)` writes `m_resolution.width = 2251`.
- Next frame: `(1)` compares `m_resolution.width(2251) != _resolution.width(2424)`
  → true → recreate.
- Recreate clamps to 2251 again → `(1)` mismatch next frame → loop.

Observed: ~650 `vkCreateSwapchainKHR` per 12 s on a level-load idle scene.
`bgfx::frameMs` 12–19 ms, all spent in `vkDeviceWaitIdle` +
`vkDestroySwapchainKHR` + `vkCreateSwapchainKHR` + image-view recreation.

This is *separate* from the SUBOPTIMAL_KHR rebuild loop documented in the
[companion PR draft](./bgfx-treat-suboptimal-as-success.md).  Both loops fire
on the same device; either alone reproduces the perf bug; closing only one
leaves the other active.

## Fix

Track the *un-clamped* requested resolution in two new shadow fields on
`RendererContextVK`.  Compare against the shadow for the "did the app ask for
something new" path; keep writing the clamped value to `m_resolution.width/
height` for everything else that reads it.

```diff
--- a/src/renderer_vk.cpp
+++ b/src/renderer_vk.cpp
@@ -2086,6 +2086,11 @@ namespace bgfx { namespace vk
 			{
 				m_resolution = _init.resolution;
 				m_resolution.reset &= ~BGFX_RESET_INTERNAL_FORCE;
+				// Prime the requested-resolution shadow so the first
+				// updateResolution() with the same dimensions doesn't
+				// false-positive a recreate.
+				m_lastRequestedWidth  = _init.resolution.width;
+				m_lastRequestedHeight = _init.resolution.height;
 
 				m_numWindows = 0;
 
@@ -3082,8 +3087,8 @@ namespace bgfx { namespace vk
 			if (false
 			||  m_resolution.formatColor        != _resolution.formatColor
 			||  m_resolution.formatDepthStencil != _resolution.formatDepthStencil
-			||  m_resolution.width              != _resolution.width
-			||  m_resolution.height             != _resolution.height
+			||  m_lastRequestedWidth            != _resolution.width
+			||  m_lastRequestedHeight           != _resolution.height
 			||  m_resolution.reset              != flags
 			||  m_backBuffer.m_swapChain.m_needToRecreateSurface
 			||  m_backBuffer.m_swapChain.m_needToRecreateSwapchain)
@@ -3097,6 +3102,11 @@ namespace bgfx { namespace vk
 
 				m_resolution = _resolution;
 				m_resolution.reset = flags;
+				// Remember the un-clamped request so the next-frame
+				// comparison above is request-vs-request, not
+				// clamped-actual-vs-request (which loops on devices
+				// where the driver permanently clamps the extent).
+				m_lastRequestedWidth  = _resolution.width;
+				m_lastRequestedHeight = _resolution.height;
 
 				m_textVideoMem.resize(false, _resolution.width, _resolution.height);
 				m_textVideoMem.clear();
@@ -4870,6 +4880,12 @@ VK_IMPORT_DEVICE
 		StateCacheLru<VkImageView, 1024> m_imageViewCache;
 
 		Resolution m_resolution;
+		// Track the last *requested* (un-clamped) resolution separately
+		// from m_resolution.width/height, which is overwritten below
+		// with the driver-clamped actual swapchain extent.  Without
+		// this, next-frame updateResolution() compares clamped-actual
+		// against the new request and triggers a rebuild loop on devices
+		// where the driver permanently clamps (display-cutout devices).
+		uint32_t m_lastRequestedWidth  = 0;
+		uint32_t m_lastRequestedHeight = 0;
 		float m_maxAnisotropy;
 		bool m_depthClamp;
 		bool m_wireframe;
```

`m_resolution.width/height` still becomes the clamped-actual value after every
recreation (existing line 3110-3111 unchanged), so every other reader of
those fields — viewport / scissor / render-target sizing, `bgfx::getStats()`,
debug overlay — sees what they currently see.  The only behavioural change is
the comparison at the top of `updateResolution()`.

## Why this is a bgfx bug regardless of platform

The current code is wrong by construction for *any* device whose Vulkan driver
clamps the requested swapchain extent down to `maxImageExtent`.  Display-cutout
devices are the most prevalent case in 2026, but in principle any device whose
`VkSurfaceCapabilitiesKHR.maxImageExtent` is smaller than what the application
requested would hit this loop.  Other possible triggers:

- High-DPI displays where the application requests at logical resolution and
  the driver clamps to physical
- Foldable devices mid-fold where surface caps shrink before the application
  notices and re-requests
- VR compositor surfaces that report a smaller useable extent than the panel

This patch is platform-neutral; the test case happens to be Android because
that's where we hit it, but the fix is correct for any of the above.

## Alternatives considered

**Don't write the clamped value back into `m_resolution.width/height` at line
3108-3111.**  Would fix the comparison loop but break every other reader of
`m_resolution.width/height` that currently expects the actual swapchain
extent.  Existing comment at `:3107` documents why the writeback is correct;
removing it would surface different bugs (viewport-too-large rendering off
the swapchain, mostly).

**Store the clamped value in a separate field (`m_actualWidth`/`Height`) and
make `m_resolution.width/height` always the requested value.**  Cleaner long
term — separates "what was asked" from "what was granted" structurally — but
invasive: every other reader of `m_resolution.width/height` would need to be
audited for which one they want.  Larger blast radius; saving for a future
cleanup if it's wanted.

**Compare against `m_backBuffer.m_width/m_height` instead of adding shadow
fields.**  Doesn't work — `m_backBuffer.m_width` IS the clamped value (it's
where the writeback at `:3110` reads from), so the comparison would still
loop.

## Test plan

Tested locally on:
- macOS 14.3 / Apple M2 Max — `engine_tests` 26981 assertions across 753 test
  cases pass.  No clamping observed (Metal backend; renderer_vk path compiles
  but isn't exercised at runtime).
- Linux x86_64 / Vulkan / RADV (Mesa 24) — full app suite runs.  Driver
  reports `maxImageExtent` matching the surface, so the existing code path
  doesn't trigger the loop and this patch is a no-op behaviourally.
- Pixel 9 / Android 16 / Vulkan / Mali-G715 — **the repro device**, applied
  *together with* the SUBOPTIMAL fix.  Before either patch: ~650
  `vkCreateSwapchainKHR` per 12 s, `bgfx::frameMs` ~16 ms.  After both: one
  `vkCreateSwapchainKHR` per process lifetime (at init), `bgfx::frameMs`
  0.27–6.27 ms, FPS ~336 at 120 Hz panel.

Suggested additional test from maintainer side: instrument
`vkCreateSwapchainKHR` count on any Android device with a display cutout,
re-run any bgfx example for 10+ seconds, confirm count is 1 (init only) and
not N (N ≈ frame count).

## Note about prior art in this repo

The two recent commits in this area (`1d8f6d8` and `edbec2f`, both
2026-06-17) refactor the *when* of swapchain recreation, not the *comparison*
that decides whether recreation is needed in the first place.  This patch
composes cleanly on top of both at the time of writing — verify at apply
time, but the changes don't overlap.

## Companion fix

See the [SUBOPTIMAL-as-success PR draft](./bgfx-treat-suboptimal-as-success.md).
Both fixes were necessary to close the Pixel 9 case; this one alone leaves the
SUBOPTIMAL rebuild loop active, and SUBOPTIMAL alone leaves this
clamped-resolution rebuild loop active.  Worth landing as two separate PRs
for bisect / review clarity but the team will recommend both.
