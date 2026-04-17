#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::core
{
struct EngineDesc;
}  // namespace engine::core

namespace engine::rendering
{
struct RenderSettings;
}  // namespace engine::rendering

namespace engine::game
{

// ---------------------------------------------------------------------------
// ProjectConfig -- startup configuration loaded from a JSON file.
//
// All fields have sensible defaults. Missing fields in the JSON keep their
// defaults. The file itself is optional.
// ---------------------------------------------------------------------------

struct WindowConfig
{
    std::string title = "Sama";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool fullscreen = false;
};

struct RenderConfig
{
    uint32_t shadowResolution = 2048;
    uint32_t shadowCascades = 1;
};

struct PhysicsConfig
{
    float fixedTimestep = 1.0f / 60.0f;
    float gravity[3] = {0.0f, -9.81f, 0.0f};
    uint32_t maxSubSteps = 4;
};

struct AudioConfig
{
    float masterVolume = 1.0f;
    float musicVolume = 0.7f;
    float sfxVolume = 1.0f;
};

// ---------------------------------------------------------------------------
// TierConfig -- bundles asset quality and render quality into a single
// developer-facing device-tier choice (e.g. "low", "mid", "high").
// Internally maps to the existing RenderSettings preset system.
// ---------------------------------------------------------------------------

struct TierConfig
{
    // Asset quality
    int maxTextureSize = 1024;
    std::string textureCompression = "astc_6x6";

    // Render quality (maps to RenderSettings)
    int shadowMapSize = 1024;
    int shadowCascades = 2;
    int maxBones = 128;
    bool enableIBL = true;
    bool enableSSAO = false;
    bool enableBloom = true;
    bool enableFXAA = true;
    bool depthPrepass = false;  // false for mobile TBDR
    float renderScale = 1.0f;
    int targetFPS = 30;
};

/// Returns the three built-in default tiers: "low", "mid", "high".
std::unordered_map<std::string, TierConfig> defaultTiers();

struct ProjectConfig
{
    std::string name = "Untitled";
    std::string startupScene;

    WindowConfig window;
    RenderConfig render;
    PhysicsConfig physics;
    AudioConfig audio;

    size_t frameArenaSize = 2 * 1024 * 1024;

    // Device tier configurations. If empty, uses built-in defaults.
    // Key: "low", "mid", "high"
    std::unordered_map<std::string, TierConfig> tiers;

    // Active tier name. Empty = auto-detect or use "mid" as default.
    std::string activeTier;

    // Get the active TierConfig. Returns built-in "mid" default if tier not found.
    TierConfig getActiveTier() const;

    // Convert a TierConfig to RenderSettings.
    static rendering::RenderSettings tierToRenderSettings(const TierConfig& tier);

    // Load from file. Returns false if file is missing or malformed.
    // Missing fields keep their defaults.
    bool loadFromFile(const char* filepath);

    // Load from an in-memory JSON string. Useful for testing.
    bool loadFromString(const char* json, size_t length);

    // Convert to EngineDesc for Engine::init().
    core::EngineDesc toEngineDesc() const;
};

}  // namespace engine::game
