#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::core
{
struct EngineDesc;
}  // namespace engine::core

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

struct ProjectConfig
{
    std::string name = "Untitled";
    std::string startupScene;

    WindowConfig window;
    RenderConfig render;
    PhysicsConfig physics;
    AudioConfig audio;

    size_t frameArenaSize = 2 * 1024 * 1024;

    // Load from file. Returns false if file is missing or malformed.
    // Missing fields keep their defaults.
    bool loadFromFile(const char* filepath);

    // Load from an in-memory JSON string. Useful for testing.
    bool loadFromString(const char* json, size_t length);

    // Convert to EngineDesc for Engine::init().
    core::EngineDesc toEngineDesc() const;
};

}  // namespace engine::game
