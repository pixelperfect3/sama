#include "engine/rendering/AndroidVulkanFormatProbe.h"

#ifdef __ANDROID__
#include <android/log.h>
#include <dlfcn.h>
// We do not pull in <vulkan/vulkan.h> at link time because we dynamically
// load libvulkan.so via dlopen.  Pulling the header in is fine for the
// type/enum definitions; we just never reference the Vulkan symbols
// directly so the loader stays soft.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#endif

#include <cstdint>
#include <vector>

namespace engine::rendering
{

namespace
{

// Vulkan format codes mirrored as raw uint32_t so the host build (and the
// host-side test binary) never need <vulkan/vulkan.h>.  These match the
// values in the Vulkan headers verbatim.
constexpr uint32_t kVkFormatR8G8B8A8Unorm = 37;           // VK_FORMAT_R8G8B8A8_UNORM
constexpr uint32_t kVkFormatB8G8R8A8Unorm = 44;           // VK_FORMAT_B8G8R8A8_UNORM
constexpr uint32_t kVkFormatA2R10G10B10UnormPack32 = 58;  // bgfx RGB10A2 match
constexpr uint32_t kVkFormatA2B10G10R10UnormPack32 = 64;  // no bgfx match -> fallback

// Priority order: we prefer RGBA8 (CDD-mandated, well-tested), then BGRA8
// (some emulators / desktop drivers), then a 10-bit format if the device
// has chosen to expose one.  See header for rationale on why
// A2B10G10R10 is not in the list.
struct PriorityEntry
{
    uint32_t vkFormat;
    bgfx::TextureFormat::Enum bgfxFormat;
};

constexpr PriorityEntry kPriorityList[] = {
    {kVkFormatR8G8B8A8Unorm, bgfx::TextureFormat::RGBA8},
    {kVkFormatB8G8R8A8Unorm, bgfx::TextureFormat::BGRA8},
    {kVkFormatA2R10G10B10UnormPack32, bgfx::TextureFormat::RGB10A2},
};

}  // namespace

const char* bgfxSwapchainFormatName(bgfx::TextureFormat::Enum f)
{
    switch (f)
    {
        case bgfx::TextureFormat::RGBA8:
            return "RGBA8";
        case bgfx::TextureFormat::BGRA8:
            return "BGRA8";
        case bgfx::TextureFormat::RGB10A2:
            return "RGB10A2";
        default:
            return "Unknown";
    }
}

bgfx::TextureFormat::Enum selectBestSwapchainFormat(const std::vector<uint32_t>& vkFormatCodes)
{
    if (vkFormatCodes.empty())
    {
        // Empty list: surface reported no formats at all.  Should never
        // happen on a conformant driver, but fall back to the CDD baseline
        // rather than crashing.
        return bgfx::TextureFormat::RGBA8;
    }

    for (const auto& entry : kPriorityList)
    {
        for (uint32_t code : vkFormatCodes)
        {
            if (code == entry.vkFormat)
            {
                return entry.bgfxFormat;
            }
        }
    }

    // No priority-list match.  Intentionally do NOT pick the first
    // reported format — we want a known-good colour space, not whatever
    // exotic format the driver decided to list first.  RGBA8 is the
    // Android CDD baseline so it is always the safest fallback.
    return bgfx::TextureFormat::RGBA8;
}

#ifdef __ANDROID__

namespace
{

constexpr const char* kLogTag = "SamaEngine";

// RAII wrapper that owns the libvulkan.so dlopen handle and exposes the
// minimum function pointers we need.  All members default to nullptr so
// callers can `if (!loader.vkCreateInstance) { return RGBA8; }` and fall
// out cleanly without calling into a missing symbol.
struct VulkanLoader
{
    void* lib = nullptr;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkDestroyInstance vkDestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;

    // Instance-level (loaded after vkCreateInstance succeeds).
    PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR = nullptr;
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;

    ~VulkanLoader()
    {
        if (lib)
        {
            dlclose(lib);
        }
    }
};

bool loadVulkanGlobals(VulkanLoader& l)
{
    // libvulkan.so is the canonical Android Vulkan loader entry point.
    // RTLD_NOW so any unresolved symbols fail here, not later.
    l.lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!l.lib)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: dlopen(libvulkan.so) failed: %s — "
                            "returning RGBA8 fallback",
                            dlerror());
        return false;
    }

    l.vkGetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(l.lib, "vkGetInstanceProcAddr"));
    if (!l.vkGetInstanceProcAddr)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: dlsym(vkGetInstanceProcAddr) failed");
        return false;
    }

    // Pre-instance globals: pass VK_NULL_HANDLE so the loader returns the
    // global function pointers for instance creation + enumeration.
    l.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
        l.vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!l.vkCreateInstance)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: vkGetInstanceProcAddr(vkCreateInstance) failed");
        return false;
    }

    return true;
}

bool loadVulkanInstanceFns(VulkanLoader& l, VkInstance instance)
{
    l.vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        l.vkGetInstanceProcAddr(instance, "vkDestroyInstance"));
    l.vkEnumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        l.vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
    l.vkCreateAndroidSurfaceKHR = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        l.vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
    l.vkDestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        l.vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR"));
    l.vkGetPhysicalDeviceSurfaceFormatsKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
            l.vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));

    if (!l.vkDestroyInstance || !l.vkEnumeratePhysicalDevices || !l.vkCreateAndroidSurfaceKHR ||
        !l.vkDestroySurfaceKHR || !l.vkGetPhysicalDeviceSurfaceFormatsKHR)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: failed to resolve one or more "
                            "instance-level functions");
        return false;
    }
    return true;
}

}  // namespace

bgfx::TextureFormat::Enum probeAndroidSwapchainFormat(ANativeWindow* window)
{
    if (!window)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: null ANativeWindow — RGBA8 fallback");
        return bgfx::TextureFormat::RGBA8;
    }

    VulkanLoader loader;
    if (!loadVulkanGlobals(loader))
    {
        return bgfx::TextureFormat::RGBA8;
    }

    // ---- vkCreateInstance with the surface extensions -----------------
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SamaEngineFormatProbe";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "SamaEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;  // matches manifest min

    const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(extensions));
    createInfo.ppEnabledExtensionNames = extensions;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult res = loader.vkCreateInstance(&createInfo, nullptr, &instance);
    if (res != VK_SUCCESS || instance == VK_NULL_HANDLE)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: vkCreateInstance failed (VkResult=%d) — "
                            "RGBA8 fallback",
                            (int)res);
        return bgfx::TextureFormat::RGBA8;
    }

    if (!loadVulkanInstanceFns(loader, instance))
    {
        loader.vkDestroyInstance(instance, nullptr);
        return bgfx::TextureFormat::RGBA8;
    }

    // ---- Pick the first physical device -------------------------------
    // bgfx will pick its own device for the real init; format support is
    // consistent across devices on the same Android driver, so the first
    // one is fine for this defensive query.
    uint32_t deviceCount = 0;
    res = loader.vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (res != VK_SUCCESS || deviceCount == 0)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: vkEnumeratePhysicalDevices count failed "
                            "(VkResult=%d count=%u) — RGBA8 fallback",
                            (int)res, deviceCount);
        loader.vkDestroyInstance(instance, nullptr);
        return bgfx::TextureFormat::RGBA8;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    res = loader.vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    if (res != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: vkEnumeratePhysicalDevices fetch failed "
                            "(VkResult=%d) — RGBA8 fallback",
                            (int)res);
        loader.vkDestroyInstance(instance, nullptr);
        return bgfx::TextureFormat::RGBA8;
    }
    VkPhysicalDevice physicalDevice = devices[0];

    // ---- Create temporary Android surface -----------------------------
    VkAndroidSurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.window = window;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    res = loader.vkCreateAndroidSurfaceKHR(instance, &surfaceInfo, nullptr, &surface);
    if (res != VK_SUCCESS || surface == VK_NULL_HANDLE)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: vkCreateAndroidSurfaceKHR failed "
                            "(VkResult=%d) — RGBA8 fallback",
                            (int)res);
        loader.vkDestroyInstance(instance, nullptr);
        return bgfx::TextureFormat::RGBA8;
    }

    // ---- Query surface formats ----------------------------------------
    uint32_t formatCount = 0;
    res =
        loader.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (res != VK_SUCCESS || formatCount == 0)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: vkGetPhysicalDeviceSurfaceFormatsKHR count "
                            "failed (VkResult=%d count=%u) — RGBA8 fallback",
                            (int)res, formatCount);
        loader.vkDestroySurfaceKHR(instance, surface, nullptr);
        loader.vkDestroyInstance(instance, nullptr);
        return bgfx::TextureFormat::RGBA8;
    }

    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    res = loader.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                                      surfaceFormats.data());
    if (res != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "VulkanFormatProbe: vkGetPhysicalDeviceSurfaceFormatsKHR fetch "
                            "failed (VkResult=%d) — RGBA8 fallback",
                            (int)res);
        loader.vkDestroySurfaceKHR(instance, surface, nullptr);
        loader.vkDestroyInstance(instance, nullptr);
        return bgfx::TextureFormat::RGBA8;
    }

    // Translate to the host-testable representation, drop the colour
    // space (we only care about the pixel layout for swapchain creation).
    std::vector<uint32_t> formatCodes;
    formatCodes.reserve(surfaceFormats.size());
    for (const auto& sf : surfaceFormats)
    {
        formatCodes.push_back(static_cast<uint32_t>(sf.format));
    }

    bgfx::TextureFormat::Enum picked = selectBestSwapchainFormat(formatCodes);

    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "VulkanFormatProbe: %u surface formats reported, picked %s", formatCount,
                        bgfxSwapchainFormatName(picked));

    // ---- Tear down ----------------------------------------------------
    loader.vkDestroySurfaceKHR(instance, surface, nullptr);
    loader.vkDestroyInstance(instance, nullptr);
    // VulkanLoader dtor closes the dlopen handle.

    return picked;
}

#endif  // __ANDROID__

}  // namespace engine::rendering
