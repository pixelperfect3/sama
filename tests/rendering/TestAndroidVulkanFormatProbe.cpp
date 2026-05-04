#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "engine/rendering/AndroidVulkanFormatProbe.h"

// ---------------------------------------------------------------------------
// TestAndroidVulkanFormatProbe — runs on the macOS host build.
//
// The full probe (vkCreateInstance, vkCreateAndroidSurfaceKHR, ...) is not
// host-testable: it needs the Vulkan loader plus a live ANativeWindow.
// What IS host-testable is the format-mapping helper
// `selectBestSwapchainFormat` — a pure function that takes the list of
// VkFormat codes the driver reports and returns the bgfx enum we should
// pass to bgfx::Init.
//
// These tests cover every branch of the priority-list walker so a refactor
// or extension of the priority list cannot silently break selection on
// real hardware.
// ---------------------------------------------------------------------------

using engine::rendering::selectBestSwapchainFormat;

namespace
{
// Mirrors the constants in AndroidVulkanFormatProbe.cpp — duplicated here so
// the test does not depend on <vulkan/vulkan.h> on the host.
constexpr uint32_t kVkR8G8B8A8Unorm = 37;
constexpr uint32_t kVkB8G8R8A8Unorm = 44;
constexpr uint32_t kVkA2R10G10B10UnormPack32 = 58;
constexpr uint32_t kVkA2B10G10R10UnormPack32 = 64;

// A handful of arbitrary formats no priority entry covers, used to verify
// the no-match fallback path.  Values come from the real Vulkan headers
// (R5G6B5 = 4, R8G8B8 = 23, R16G16B16A16_SFLOAT = 97).
constexpr uint32_t kVkR5G6B5UnormPack16 = 4;
constexpr uint32_t kVkR8G8B8Unorm = 23;
constexpr uint32_t kVkR16G16B16A16Sfloat = 97;
}  // namespace

TEST_CASE("selectBestSwapchainFormat — RGBA8 only returns RGBA8", "[android][vulkan][format_probe]")
{
    std::vector<uint32_t> formats = {kVkR8G8B8A8Unorm};
    REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::RGBA8);
}

TEST_CASE("selectBestSwapchainFormat — BGRA8 only returns BGRA8", "[android][vulkan][format_probe]")
{
    std::vector<uint32_t> formats = {kVkB8G8R8A8Unorm};
    REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::BGRA8);
}

TEST_CASE("selectBestSwapchainFormat — RGBA8 + BGRA8 prefers RGBA8 (priority order)",
          "[android][vulkan][format_probe]")
{
    // Order in the input list must NOT matter — we walk the priority list,
    // not the surface list, so RGBA8 wins regardless of which the driver
    // listed first.
    {
        std::vector<uint32_t> formats = {kVkR8G8B8A8Unorm, kVkB8G8R8A8Unorm};
        REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::RGBA8);
    }
    {
        std::vector<uint32_t> formats = {kVkB8G8R8A8Unorm, kVkR8G8B8A8Unorm};
        REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::RGBA8);
    }
}

TEST_CASE("selectBestSwapchainFormat — A2R10G10B10 matches RGB10A2 when 8-bit absent",
          "[android][vulkan][format_probe]")
{
    std::vector<uint32_t> formats = {kVkA2R10G10B10UnormPack32};
    REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::RGB10A2);
}

TEST_CASE("selectBestSwapchainFormat — A2B10G10R10 alone falls back to RGBA8",
          "[android][vulkan][format_probe]")
{
    // A2B10G10R10 has no exact bgfx match (bgfx's RGB10A2 is the
    // A2R10G10B10 layout), so we deliberately do not include it in the
    // priority list — fall back to the CDD baseline.
    std::vector<uint32_t> formats = {kVkA2B10G10R10UnormPack32};
    REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::RGBA8);
}

TEST_CASE("selectBestSwapchainFormat — no priority match falls back to RGBA8",
          "[android][vulkan][format_probe]")
{
    // Random non-priority formats: 5-6-5 packed, 24-bit RGB, half-float.
    // None of these can drive a bgfx swapchain via our enum table, so we
    // pick the safe RGBA8 default rather than an exotic format.
    std::vector<uint32_t> formats = {kVkR5G6B5UnormPack16, kVkR8G8B8Unorm, kVkR16G16B16A16Sfloat};
    REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::RGBA8);
}

TEST_CASE("selectBestSwapchainFormat — empty list falls back to RGBA8",
          "[android][vulkan][format_probe]")
{
    // Should never happen on a conformant driver, but be safe.
    std::vector<uint32_t> formats;
    REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::RGBA8);
}

TEST_CASE("selectBestSwapchainFormat — RGBA8 + 10-bit prefers RGBA8 over higher bit depth",
          "[android][vulkan][format_probe]")
{
    // Reasoning: the priority list deliberately ranks 8-bit RGBA8 above
    // the 10-bit format because the rest of the engine targets 8-bit
    // sRGB by default.  A future opt-in HDR pipeline would change the
    // priority list (or take a parameter); until then 8-bit wins.
    std::vector<uint32_t> formats = {kVkA2R10G10B10UnormPack32, kVkR8G8B8A8Unorm};
    REQUIRE(selectBestSwapchainFormat(formats) == bgfx::TextureFormat::RGBA8);
}
