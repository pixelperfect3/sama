# Fix Vulkan swapchain image count on Android (NUM_SWAPCHAIN_IMAGE)

## Summary

Increase the hardcoded `NUM_SWAPCHAIN_IMAGE` from 4 to 8 and wrap it in an
`#ifndef` guard so it can be overridden at compile time. This fixes Vulkan
initialization failures on modern Android devices (e.g., Google Pixel 9) where
the driver requests a minimum of 5 swapchain images.

## Background

Modern Android devices with Mali GPUs (Arm Immortalis-G720) running Android 14+
and Vulkan 1.3 report `VkSurfaceCapabilitiesKHR.minImageCount = 5`. bgfx
correctly passes this value to `vkCreateSwapchainKHR`, but the fixed-size arrays
(`m_backBufferColorImage`, `m_backBufferColorImageLayout`,
`m_backBufferColorImageView`) are only 4 elements wide due to:

```cpp
#define NUM_SWAPCHAIN_IMAGE 4
```

When `vkGetSwapchainImagesKHR` returns 5 images, bgfx detects the overflow and
aborts initialization, silently falling back to OpenGL ES 2.0.

## How to Reproduce

1. Build a bgfx application targeting Android with Vulkan.
2. Deploy to a Google Pixel 9 (Mali GPU, Android 14, Vulkan 1.3).
3. Request `bgfx::RendererType::Vulkan` in `bgfx::Init`.
4. Observe failure:

```
Create swapchain error: vkGetSwapchainImagesKHR: numSwapchainImages 5 > countof(m_backBufferColorImage) 4.
Init error: creating swapchain and image view failed -3: VK_ERROR_INITIALIZATION_FAILED
```

## The Fix

### Minimal fix (this PR)

```cpp
// Before:
#define NUM_SWAPCHAIN_IMAGE 4

// After:
#ifndef NUM_SWAPCHAIN_IMAGE
#define NUM_SWAPCHAIN_IMAGE 8
#endif
```

**Why 8?** The maximum observed `minImageCount` in the wild is 5. A value of 8
provides comfortable headroom for future devices without meaningful memory cost
(a few extra `VkImage` handles and `VkImageView` pointers). The `#ifndef` guard
allows projects with tighter constraints to override at compile time via
`-DNUM_SWAPCHAIN_IMAGE=N`.

### Ideal fix (future work)

A more robust solution would query `VkSurfaceCapabilitiesKHR.minImageCount` at
runtime and dynamically size the arrays. This is a larger refactor and is left
for a follow-up PR.

## Testing

- **Google Pixel 9 (Mali, Android 14, Vulkan 1.3):** Vulkan now initializes
  successfully. Swapchain created with 5 images, all slots fit within the
  8-element arrays.
- **Desktop (NVIDIA, Vulkan 1.3):** No regression. Swapchain still uses 3
  images as before.
- **Samsung Galaxy S23 (Adreno, Android 13, Vulkan 1.1):** No regression.
  Swapchain uses 3 images.
- **Compile-time override:** Verified that `-DNUM_SWAPCHAIN_IMAGE=6` correctly
  overrides the default.

## Related Issues

- Fixes #3567
- Related to #3242, #3302
