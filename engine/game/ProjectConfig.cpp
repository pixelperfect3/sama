#include "engine/game/ProjectConfig.h"

#include "engine/core/Engine.h"
#include "engine/io/Json.h"

namespace engine::game
{

static void parseConfig(ProjectConfig& config, io::JsonValue root)
{
    if (!root.isObject())
        return;

    // Top-level fields
    if (root.hasMember("name") && root["name"].isString())
        config.name = root["name"].getString();

    if (root.hasMember("startupScene") && root["startupScene"].isString())
        config.startupScene = root["startupScene"].getString();

    if (root.hasMember("frameArenaSize") && root["frameArenaSize"].isUint())
        config.frameArenaSize = static_cast<size_t>(root["frameArenaSize"].getUint());

    // Window
    if (root.hasMember("window"))
    {
        auto w = root["window"];
        if (w.isObject())
        {
            if (w.hasMember("title") && w["title"].isString())
                config.window.title = w["title"].getString();
            if (w.hasMember("width"))
                config.window.width = w["width"].getUint(config.window.width);
            if (w.hasMember("height"))
                config.window.height = w["height"].getUint(config.window.height);
            if (w.hasMember("fullscreen"))
                config.window.fullscreen = w["fullscreen"].getBool(config.window.fullscreen);
        }
    }

    // Render
    if (root.hasMember("render"))
    {
        auto r = root["render"];
        if (r.isObject())
        {
            if (r.hasMember("shadowResolution"))
                config.render.shadowResolution =
                    r["shadowResolution"].getUint(config.render.shadowResolution);
            if (r.hasMember("shadowCascades"))
                config.render.shadowCascades =
                    r["shadowCascades"].getUint(config.render.shadowCascades);
        }
    }

    // Physics
    if (root.hasMember("physics"))
    {
        auto p = root["physics"];
        if (p.isObject())
        {
            if (p.hasMember("fixedTimestep"))
                config.physics.fixedTimestep =
                    p["fixedTimestep"].getFloat(config.physics.fixedTimestep);
            if (p.hasMember("fixedRateHz"))
            {
                uint32_t hz = p["fixedRateHz"].getUint(60);
                if (hz > 0)
                    config.physics.fixedTimestep = 1.0f / static_cast<float>(hz);
            }
            if (p.hasMember("gravity") && p["gravity"].isArray() && p["gravity"].arraySize() == 3)
            {
                auto g = p["gravity"];
                config.physics.gravity[0] = g[static_cast<size_t>(0)].getFloat(0.0f);
                config.physics.gravity[1] = g[static_cast<size_t>(1)].getFloat(-9.81f);
                config.physics.gravity[2] = g[static_cast<size_t>(2)].getFloat(0.0f);
            }
            if (p.hasMember("maxSubSteps"))
                config.physics.maxSubSteps = p["maxSubSteps"].getUint(config.physics.maxSubSteps);
        }
    }

    // Audio
    if (root.hasMember("audio"))
    {
        auto a = root["audio"];
        if (a.isObject())
        {
            if (a.hasMember("masterVolume"))
                config.audio.masterVolume = a["masterVolume"].getFloat(config.audio.masterVolume);
            if (a.hasMember("musicVolume"))
                config.audio.musicVolume = a["musicVolume"].getFloat(config.audio.musicVolume);
            if (a.hasMember("sfxVolume"))
                config.audio.sfxVolume = a["sfxVolume"].getFloat(config.audio.sfxVolume);
        }
    }
}

bool ProjectConfig::loadFromFile(const char* filepath)
{
    io::JsonDocument doc;
    if (!doc.parseFile(filepath))
        return false;

    parseConfig(*this, doc.root());
    return true;
}

bool ProjectConfig::loadFromString(const char* json, size_t length)
{
    io::JsonDocument doc;
    if (!doc.parse(json, length))
        return false;

    parseConfig(*this, doc.root());
    return true;
}

core::EngineDesc ProjectConfig::toEngineDesc() const
{
    core::EngineDesc desc;
    desc.windowWidth = window.width;
    desc.windowHeight = window.height;
    desc.windowTitle = window.title.c_str();
    desc.shadowResolution = render.shadowResolution;
    desc.shadowCascades = render.shadowCascades;
    desc.frameArenaSize = frameArenaSize;
    return desc;
}

}  // namespace engine::game
