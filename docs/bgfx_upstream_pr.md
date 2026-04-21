# Fix Vulkan swapchain image count on Android (NUM_SWAPCHAIN_IMAGE)

## STATUS: ALREADY FIXED UPSTREAM

**This issue has already been fixed in bgfx master.** The latest `renderer_vk.h` uses:
```cpp
constexpr uint32_t kMaxBackBuffers = bx::max(BGFX_CONFIG_MAX_BACK_BUFFERS, 10);
```
which replaces the old `#define NUM_SWAPCHAIN_IMAGE 4`. No upstream PR is needed.

**Action item:** Update our bgfx.cmake dependency (`CMakeLists.txt`, `GIT_TAG`) to a
newer commit that includes this fix, then remove the CMake patch in our build.

---

## Original Summary (for reference)

Two fixes for Vulkan swapchain creation failures on modern Android devices:

1. **Raise `NUM_SWAPCHAIN_IMAGE` from 4 to 8** with `#ifndef` guard for compile-time override
2. **Clamp `minImageCount` to array capacity** with a clear warning instead of silently failing

This fixes Vulkan initialization failures on devices like the Google Pixel 9 where the driver requests a minimum of 5 swapchain images.

## Background

Modern Android devices with Mali GPUs (Arm Immortalis-G720) running Android 14+
and Vulkan 1.3 report `VkSurfaceCapabilitiesKHR.minImageCount = 5`. bgfx
correctly passes this value to `vkCreateSwapchainKHR`, but the fixed-size arrays
(`m_backBufferColorImage`, `m_backBufferColorImageLayout`,
`m_backBufferColorImageView`, etc.) are only 4 elements wide due to:

```cpp
#define NUM_SWAPCHAIN_IMAGE 4  // renderer_vk.cpp line 4238
```

When `vkGetSwapchainImagesKHR` returns 5 images, bgfx detects the overflow and
aborts initialization, silently falling back to OpenGL ES 2.0. The application
has no way to know this happened — `bgfx::init()` returns true and
`bgfx::getRendererType()` reports `OpenGLES` even though `Vulkan` was requested.

## How to Reproduce

1. Build a bgfx application targeting Android with Vulkan
2. Deploy to a Google Pixel 9 (Mali GPU, Android 14, Vulkan 1.3)
3. Request `bgfx::RendererType::Vulkan` in `bgfx::Init`
4. Enable `BGFX_CONFIG_DEBUG=1` to see trace output
5. Observe failure:

```
BGFX Create swapchain error: vkGetSwapchainImagesKHR: numSwapchainImages 5 > countof(m_backBufferColorImage) 4.
BGFX Init error: creating swapchain and image view failed -3: VK_ERROR_INITIALIZATION_FAILED
BGFX errorState 4
```

bgfx then falls back to OpenGL ES 2.0 without any error returned to the caller.

## The Fix

### Change 1: Raise `NUM_SWAPCHAIN_IMAGE` with override guard

```cpp
// Before (renderer_vk.cpp, line ~4238):
#define NUM_SWAPCHAIN_IMAGE 4

// After:
// Maximum number of swapchain images the renderer can manage.
// Modern Android devices (e.g., Pixel 9 with Mali GPU) report
// minImageCount=5 from vkGetPhysicalDeviceSurfaceCapabilitiesKHR.
// Override at compile time with -DNUM_SWAPCHAIN_IMAGE=N if needed.
#ifndef NUM_SWAPCHAIN_IMAGE
#define NUM_SWAPCHAIN_IMAGE 8
#endif // NUM_SWAPCHAIN_IMAGE
```

**Why 8?** The maximum observed `minImageCount` in the wild is 5. A value of 8
provides comfortable headroom for future devices without meaningful memory cost
(a few extra `VkImage` handles and `VkImageView` pointers). The `#ifndef` guard
allows projects with tighter constraints to override at compile time.

### Change 2: Clamp `minImageCount` to array capacity

```cpp
// Before (renderer_vk.cpp, swapchain creation ~line 2031):
m_sci.minImageCount = surfaceCapabilities.minImageCount;

// After:
uint32_t desiredImageCount = surfaceCapabilities.minImageCount;
if (surfaceCapabilities.maxImageCount > 0
&&  desiredImageCount > surfaceCapabilities.maxImageCount)
{
    desiredImageCount = surfaceCapabilities.maxImageCount;
}
if (desiredImageCount > NUM_SWAPCHAIN_IMAGE)
{
    BX_TRACE("Warning: device minImageCount %d > NUM_SWAPCHAIN_IMAGE %d, clamping. "
        "Recompile with -DNUM_SWAPCHAIN_IMAGE=%d to fix.",
        desiredImageCount, NUM_SWAPCHAIN_IMAGE, desiredImageCount);
    desiredImageCount = NUM_SWAPCHAIN_IMAGE;
}
m_sci.minImageCount = desiredImageCount;
```

This follows the Vulkan best practice of clamping between `minImageCount` and
`maxImageCount` (see [Khronos Vulkan Samples: swapchain_images](https://github.com/KhronosGroup/Vulkan-Samples/tree/main/samples/performance/swapchain_images)).

### Ideal future fix

A more robust solution would replace the fixed-size arrays with dynamically-sized
storage based on the actual swapchain image count returned by the driver. This is
a larger refactor involving `m_backBufferColorImage`, `m_backBufferColorImageView`,
`m_backBufferColor`, `m_commandBuffers`, `m_scratchBuffer`, and `m_presentDone`.

## Testing

| Device | GPU | OS | Vulkan | minImageCount | Result |
|--------|-----|----|--------|---------------|--------|
| Google Pixel 9 | Mali (Immortalis-G720) | Android 14 | 1.3 | 5 | Vulkan works (was GLES fallback) |
| Desktop (macOS) | Apple M-series | macOS 14 | Metal | N/A | No regression |
| Desktop (Linux) | NVIDIA | Ubuntu | 1.3 | 3 | No regression |

## Related Issues

- May fix #3242 (Android frame update problem — first frame only with Vulkan)
- Related to #3567 (Vulkan swapchain semaphore reuse)
- Related to #3302 (synchronization issue)
