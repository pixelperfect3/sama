# Vulkan init fails on Android devices requesting >4 swapchain images

## Environment

- **Device:** Google Pixel 9
- **GPU:** Mali (Arm Immortalis-G720)
- **OS:** Android 14 (API 34+)
- **Vulkan version:** 1.3
- **bgfx version:** latest main (also reproduced on 1.127+)

## Description

bgfx's Vulkan renderer fails to initialize on modern Android devices where the
Vulkan driver requests a minimum of 5 swapchain images. The hardcoded constant
`NUM_SWAPCHAIN_IMAGE 4` in `renderer_vk.cpp` (line ~4238) limits the backing
arrays to 4 elements, but `vkGetSwapchainImagesKHR` returns 5 images on these
devices.

This causes bgfx to abort Vulkan initialization and silently fall back to
OpenGL ES 2.0, even when the application explicitly requests
`bgfx::RendererType::Vulkan`.

## Steps to Reproduce

1. Build any bgfx application targeting Vulkan on Android.
2. Deploy to a Google Pixel 9 (or similar device with Mali GPU running
   Android 14+ and Vulkan 1.3).
3. Initialize bgfx with `init.type = bgfx::RendererType::Vulkan`.
4. Observe that Vulkan initialization fails and the renderer falls back to
   OpenGL ES.

## Expected Behavior

bgfx should successfully create the Vulkan swapchain regardless of how many
images the driver requires, as long as the hardware supports Vulkan.

## Actual Behavior

bgfx logs the following error and falls back to OpenGL ES 2.0:

```
Create swapchain error: vkGetSwapchainImagesKHR: numSwapchainImages 5 > countof(m_backBufferColorImage) 4.
Init error: creating swapchain and image view failed -3: VK_ERROR_INITIALIZATION_FAILED
```

## Root Cause Analysis

In `renderer_vk.cpp`, the swapchain image arrays are sized by a compile-time
constant:

```cpp
#define NUM_SWAPCHAIN_IMAGE 4
```

The arrays `m_backBufferColorImage`, `m_backBufferColorImageLayout`, and
`m_backBufferColorImageView` are all declared with this fixed size.

When bgfx calls `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`, the returned
`VkSurfaceCapabilitiesKHR.minImageCount` is 5 on affected devices. bgfx already
queries this value and passes it to `vkCreateSwapchainKHR`, but the backing
arrays cannot hold more than 4 images. The subsequent call to
`vkGetSwapchainImagesKHR` returns 5 images, triggering the size check failure.

The code at the point of failure looks like:

```cpp
if (numSwapchainImages > BX_COUNTOF(m_backBufferColorImage))
{
    BX_TRACE("Create swapchain error: vkGetSwapchainImagesKHR: "
             "numSwapchainImages %d > countof(m_backBufferColorImage) %d."
             , numSwapchainImages
             , BX_COUNTOF(m_backBufferColorImage));
    return VK_ERROR_INITIALIZATION_FAILED;
}
```

## Suggested Fix

### Minimal (backward-compatible)

Wrap the define in an `#ifndef` guard and increase the default to 8:

```cpp
#ifndef NUM_SWAPCHAIN_IMAGE
#define NUM_SWAPCHAIN_IMAGE 8
#endif
```

This accommodates all known devices (max observed `minImageCount` is 5) with
headroom, and allows users to override via `-DNUM_SWAPCHAIN_IMAGE=N` at compile
time.

### Ideal (more involved)

Query `VkSurfaceCapabilitiesKHR.minImageCount` at runtime and use it (clamped
to `maxImageCount`) to dynamically size the arrays, or switch to
`std::vector` / `bx::Array` for the back buffer storage.

## Related Issues

- #3567 — Vulkan swapchain issues on Android
- #3242 — Swapchain image count assumptions
- #3302 — Vulkan initialization failures on newer Android devices
