#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

struct ANativeWindow;

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// AndroidVulkanFormatProbe
//
// Defensive pre-init Vulkan surface format probe used to feed
// `bgfx::Init::resolution.formatColor` on Android.
//
// Why pre-init?  bgfx exposes `getCaps()->formats[]` for surface-format
// support, but only AFTER `bgfx::init` runs — and by then the swapchain has
// already been created with whatever `formatColor` we passed in.  If that
// format is not supported, swapchain creation fails silently and bgfx falls
// back to OpenGL ES.  RGBA8 is mandatory on every Vulkan-capable Android
// device per the Android CDD, so the historical hardcoded value works on all
// real hardware today, but a future device or rendering target could need
// something different (e.g. a 10-bit HDR swapchain).  This probe queries
// `vkGetPhysicalDeviceSurfaceFormatsKHR` against a temporary
// `VkInstance` + `VkSurfaceKHR` BEFORE bgfx ever sees the platform handle,
// so we can pass bgfx a known-good color format on every device.
//
// Why dynamic loading?  The probe `dlopen`s `libvulkan.so` rather than
// linking against it.  This means a hypothetical future device that drops
// Vulkan support still loads the binary cleanly — the `dlopen` just fails
// and we return the safe RGBA8 default.  See `docs/NOTES.md` for the
// dynamic-loading vs link-time tradeoff write-up.
// ---------------------------------------------------------------------------

// Selects the best `bgfx::TextureFormat::Enum` from a list of
// `VkSurfaceFormatKHR` entries reported by
// `vkGetPhysicalDeviceSurfaceFormatsKHR`.  Walks a fixed priority list:
//
//     RGBA8 -> BGRA8 -> RGB10A2 (A2R10G10B10) -> fallback (RGBA8)
//
// `VK_FORMAT_A2B10G10R10_UNORM_PACK32` has no exact bgfx match (bgfx's
// `RGB10A2` is `A2R10G10B10`), so it is intentionally NOT in the priority
// list — if the surface only reports the BGR-order packed format we fall
// back to the CDD-mandated RGBA8 baseline rather than picking a
// silently-mismatched bgfx enum.
//
// Inputs are passed as a `std::vector<uint32_t>` of raw `VkFormat` codes so
// the helper is testable without dragging the Vulkan headers into the
// host-side test binary.  The probe entry point translates the real
// `VkSurfaceFormatKHR` array into this representation before calling.
//
// Returns `bgfx::TextureFormat::RGBA8` on:
//   - empty list (no surface formats reported)
//   - no priority-list match
// because RGBA8 is the only format guaranteed to exist on every Vulkan
// Android device per the Android CDD.  Picking the *first reported* format
// instead would risk landing in an unexpected colour space (e.g. sRGB vs
// UNORM) on devices that report uncommon formats first.
[[nodiscard]] bgfx::TextureFormat::Enum selectBestSwapchainFormat(
    const std::vector<uint32_t>& vkFormatCodes);

// Tiny lookup table for the bgfx texture-format enums the probe can produce.
// Used for logcat output (bgfx itself does not expose a public format-name
// helper).  Returns "Unknown" for formats outside the probe's priority list.
[[nodiscard]] const char* bgfxSwapchainFormatName(bgfx::TextureFormat::Enum f);

#ifdef __ANDROID__
// Probe the Vulkan device for a supported swapchain colour format BEFORE
// `bgfx::init` runs.  Returns the best match from
// `selectBestSwapchainFormat`'s priority list.
//
// On any failure (Vulkan loader missing, surface creation fails, no
// supported formats), returns `bgfx::TextureFormat::RGBA8` — the
// Android CDD-mandated baseline that every Vulkan device must support.
// This makes the function strictly safer than the old hardcoded path:
// same default, plus correct detection when possible.
//
// Logs the picked format and the path taken (success / which fallback)
// to logcat under the `SamaEngine` tag so operators can see in
// `adb logcat` which branch executed on a given device.
[[nodiscard]] bgfx::TextureFormat::Enum probeAndroidSwapchainFormat(ANativeWindow* window);
#endif  // __ANDROID__

}  // namespace engine::rendering
