#include "engine/game/ProjectConfig.h"

#include "engine/core/Engine.h"
#include "engine/io/Json.h"
#include "engine/rendering/RenderSettings.h"

namespace engine::game
{

// ---------------------------------------------------------------------------
// Default tiers
// ---------------------------------------------------------------------------

std::unordered_map<std::string, TierConfig> defaultTiers()
{
    std::unordered_map<std::string, TierConfig> tiers;

    // Low — weak mobile hardware
    {
        TierConfig t;
        t.maxTextureSize = 512;
        t.textureCompression = "astc_8x8";
        t.shadowMapSize = 512;
        t.shadowCascades = 1;
        t.maxBones = 64;
        t.enableIBL = false;
        t.enableSSAO = false;
        t.enableBloom = false;
        t.enableFXAA = true;
        t.depthPrepass = false;
        t.renderScale = 0.75f;
        t.targetFPS = 30;
        tiers["low"] = t;
    }

    // Mid — mainstream mobile
    {
        TierConfig t;
        t.maxTextureSize = 1024;
        t.textureCompression = "astc_6x6";
        t.shadowMapSize = 1024;
        t.shadowCascades = 2;
        t.maxBones = 128;
        t.enableIBL = true;
        t.enableSSAO = false;
        t.enableBloom = true;
        t.enableFXAA = true;
        t.depthPrepass = false;
        t.renderScale = 1.0f;
        t.targetFPS = 30;
        tiers["mid"] = t;
    }

    // High — flagship mobile
    {
        TierConfig t;
        t.maxTextureSize = 2048;
        t.textureCompression = "astc_4x4";
        t.shadowMapSize = 2048;
        t.shadowCascades = 3;
        t.maxBones = 128;
        t.enableIBL = true;
        t.enableSSAO = true;
        t.enableBloom = true;
        t.enableFXAA = true;
        t.depthPrepass = false;
        t.renderScale = 1.0f;
        t.targetFPS = 60;
        tiers["high"] = t;
    }

    return tiers;
}

// ---------------------------------------------------------------------------
// getActiveTier
// ---------------------------------------------------------------------------

TierConfig ProjectConfig::getActiveTier() const
{
    // Determine the effective tier name.
    std::string tierName = activeTier.empty() ? "mid" : activeTier;

    // Look in user-defined tiers first.
    if (!tiers.empty())
    {
        auto it = tiers.find(tierName);
        if (it != tiers.end())
            return it->second;
    }

    // Fall back to built-in defaults.
    auto defaults = defaultTiers();
    auto it = defaults.find(tierName);
    if (it != defaults.end())
        return it->second;

    // If even the requested name is unknown, return "mid".
    return defaults.at("mid");
}

// ---------------------------------------------------------------------------
// tierToRenderSettings
// ---------------------------------------------------------------------------

rendering::RenderSettings ProjectConfig::tierToRenderSettings(const TierConfig& tier)
{
    rendering::RenderSettings rs;

    rs.shadows.directionalRes = static_cast<uint16_t>(tier.shadowMapSize);
    rs.shadows.cascadeCount = static_cast<uint8_t>(tier.shadowCascades);
    rs.shadows.filter = (tier.shadowMapSize >= 2048) ? rendering::ShadowFilter::PCF4x4
                                                     : rendering::ShadowFilter::Hard;

    rs.lighting.iblEnabled = tier.enableIBL;

    rs.postProcess.ssao.enabled = tier.enableSSAO;
    rs.postProcess.bloom.enabled = tier.enableBloom;
    rs.postProcess.fxaaEnabled = tier.enableFXAA;

    rs.depthPrepassEnabled = tier.depthPrepass;
    rs.renderScale = tier.renderScale;

    return rs;
}

// ---------------------------------------------------------------------------
// parseTierConfig — parse a single TierConfig from a JSON object
// ---------------------------------------------------------------------------

static TierConfig parseTierConfig(io::JsonValue obj, TierConfig base = TierConfig{})
{
    if (!obj.isObject())
        return base;

    if (obj.hasMember("maxTextureSize"))
        base.maxTextureSize = obj["maxTextureSize"].getInt(base.maxTextureSize);
    if (obj.hasMember("textureCompression") && obj["textureCompression"].isString())
        base.textureCompression = obj["textureCompression"].getString();
    if (obj.hasMember("shadowMapSize"))
        base.shadowMapSize = obj["shadowMapSize"].getInt(base.shadowMapSize);
    if (obj.hasMember("shadowCascades"))
        base.shadowCascades = obj["shadowCascades"].getInt(base.shadowCascades);
    if (obj.hasMember("maxBones"))
        base.maxBones = obj["maxBones"].getInt(base.maxBones);
    if (obj.hasMember("enableIBL"))
        base.enableIBL = obj["enableIBL"].getBool(base.enableIBL);
    if (obj.hasMember("enableSSAO"))
        base.enableSSAO = obj["enableSSAO"].getBool(base.enableSSAO);
    if (obj.hasMember("enableBloom"))
        base.enableBloom = obj["enableBloom"].getBool(base.enableBloom);
    if (obj.hasMember("enableFXAA"))
        base.enableFXAA = obj["enableFXAA"].getBool(base.enableFXAA);
    if (obj.hasMember("depthPrepass"))
        base.depthPrepass = obj["depthPrepass"].getBool(base.depthPrepass);
    if (obj.hasMember("renderScale"))
        base.renderScale = obj["renderScale"].getFloat(base.renderScale);
    if (obj.hasMember("targetFPS"))
        base.targetFPS = obj["targetFPS"].getInt(base.targetFPS);

    return base;
}

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

    // Active tier
    if (root.hasMember("activeTier") && root["activeTier"].isString())
        config.activeTier = root["activeTier"].getString();

    // Tiers
    if (root.hasMember("tiers") && root["tiers"].isObject())
    {
        auto tiersObj = root["tiers"];
        for (auto member : tiersObj)
        {
            const char* tierName = member.memberName();
            if (tierName && member.isObject())
            {
                config.tiers[tierName] = parseTierConfig(member);
            }
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
    desc.frameArenaSize = frameArenaSize;

    // If tiers are configured (or an activeTier is set), the active tier's
    // render settings override the legacy RenderConfig fields.
    if (!tiers.empty() || !activeTier.empty())
    {
        TierConfig tier = getActiveTier();
        desc.shadowResolution = static_cast<uint32_t>(tier.shadowMapSize);
        desc.shadowCascades = static_cast<uint32_t>(tier.shadowCascades);
    }
    else
    {
        desc.shadowResolution = render.shadowResolution;
        desc.shadowCascades = render.shadowCascades;
    }

    return desc;
}

}  // namespace engine::game
