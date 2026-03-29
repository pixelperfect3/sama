#include "engine/rendering/RenderSettingsJson.h"

#include <cstring>

#include "engine/io/Json.h"

namespace engine::rendering
{

// ---------------------------------------------------------------------------
// Enum string tables
// ---------------------------------------------------------------------------

static ShadowFilter parseShadowFilter(const char* str, ShadowFilter fallback)
{
    if (!str)
        return fallback;
    if (std::strcmp(str, "Hard") == 0)
        return ShadowFilter::Hard;
    if (std::strcmp(str, "PCF4x4") == 0)
        return ShadowFilter::PCF4x4;
    if (std::strcmp(str, "PCF8x8") == 0)
        return ShadowFilter::PCF8x8;
    return fallback;
}

static const char* shadowFilterToString(ShadowFilter f)
{
    switch (f)
    {
    case ShadowFilter::Hard:
        return "Hard";
    case ShadowFilter::PCF4x4:
        return "PCF4x4";
    case ShadowFilter::PCF8x8:
        return "PCF8x8";
    }
    return "PCF4x4";
}

static ToneMapper parseToneMapper(const char* str, ToneMapper fallback)
{
    if (!str)
        return fallback;
    if (std::strcmp(str, "ACES") == 0)
        return ToneMapper::ACES;
    if (std::strcmp(str, "Reinhard") == 0)
        return ToneMapper::Reinhard;
    if (std::strcmp(str, "Uncharted2") == 0)
        return ToneMapper::Uncharted2;
    return fallback;
}

static const char* toneMapperToString(ToneMapper t)
{
    switch (t)
    {
    case ToneMapper::ACES:
        return "ACES";
    case ToneMapper::Reinhard:
        return "Reinhard";
    case ToneMapper::Uncharted2:
        return "Uncharted2";
    }
    return "ACES";
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

RenderSettings loadRenderSettings(const char* filepath, const GpuFeatures& gpu)
{
    RenderSettings s = renderSettingsPlatformDefault(gpu);

    io::JsonDocument doc;
    if (!doc.parseFile(filepath))
    {
        return s;
    }

    auto root = doc.root();
    if (!root.isObject())
    {
        return s;
    }

    // shadows
    auto shadows = root["shadows"];
    if (shadows.isObject())
    {
        s.shadows.enabled = shadows["enabled"].getBool(s.shadows.enabled);
        s.shadows.directionalRes =
            static_cast<uint16_t>(shadows["directionalRes"].getUint(s.shadows.directionalRes));
        s.shadows.spotRes =
            static_cast<uint16_t>(shadows["spotRes"].getUint(s.shadows.spotRes));
        s.shadows.cascadeCount =
            static_cast<uint8_t>(shadows["cascadeCount"].getUint(s.shadows.cascadeCount));
        s.shadows.maxDistance = shadows["maxDistance"].getFloat(s.shadows.maxDistance);
        s.shadows.filter =
            parseShadowFilter(shadows["filter"].getString(nullptr), s.shadows.filter);
    }

    // lighting
    auto lighting = root["lighting"];
    if (lighting.isObject())
    {
        s.lighting.maxActiveLights =
            static_cast<uint16_t>(lighting["maxActiveLights"].getUint(s.lighting.maxActiveLights));
        s.lighting.iblEnabled = lighting["iblEnabled"].getBool(s.lighting.iblEnabled);
    }

    // postProcess
    auto pp = root["postProcess"];
    if (pp.isObject())
    {
        // ssao
        auto ssao = pp["ssao"];
        if (ssao.isObject())
        {
            s.postProcess.ssao.enabled = ssao["enabled"].getBool(s.postProcess.ssao.enabled);
            s.postProcess.ssao.radius = ssao["radius"].getFloat(s.postProcess.ssao.radius);
            s.postProcess.ssao.bias = ssao["bias"].getFloat(s.postProcess.ssao.bias);
            s.postProcess.ssao.sampleCount =
                static_cast<uint8_t>(ssao["sampleCount"].getUint(s.postProcess.ssao.sampleCount));
        }

        // bloom
        auto bloom = pp["bloom"];
        if (bloom.isObject())
        {
            s.postProcess.bloom.enabled = bloom["enabled"].getBool(s.postProcess.bloom.enabled);
            s.postProcess.bloom.threshold =
                bloom["threshold"].getFloat(s.postProcess.bloom.threshold);
            s.postProcess.bloom.intensity =
                bloom["intensity"].getFloat(s.postProcess.bloom.intensity);
            s.postProcess.bloom.downsampleSteps = static_cast<uint8_t>(
                bloom["downsampleSteps"].getUint(s.postProcess.bloom.downsampleSteps));
        }

        s.postProcess.fxaaEnabled = pp["fxaaEnabled"].getBool(s.postProcess.fxaaEnabled);
        s.postProcess.toneMapper =
            parseToneMapper(pp["toneMapper"].getString(nullptr), s.postProcess.toneMapper);
        s.postProcess.exposure = pp["exposure"].getFloat(s.postProcess.exposure);
    }

    // top-level fields
    s.depthPrepassEnabled = root["depthPrepassEnabled"].getBool(s.depthPrepassEnabled);
    s.depthPrepassAlphaTestedOnly =
        root["depthPrepassAlphaTestedOnly"].getBool(s.depthPrepassAlphaTestedOnly);
    s.anisotropicFiltering =
        static_cast<uint8_t>(root["anisotropicFiltering"].getUint(s.anisotropicFiltering));
    s.renderScale = root["renderScale"].getFloat(s.renderScale);

    return s;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

bool saveRenderSettings(const RenderSettings& settings, const char* filepath)
{
    io::JsonWriter w(true);

    w.startObject();

    // shadows
    w.key("shadows");
    w.startObject();
    w.key("enabled");
    w.writeBool(settings.shadows.enabled);
    w.key("directionalRes");
    w.writeUint(settings.shadows.directionalRes);
    w.key("spotRes");
    w.writeUint(settings.shadows.spotRes);
    w.key("cascadeCount");
    w.writeUint(settings.shadows.cascadeCount);
    w.key("maxDistance");
    w.writeFloat(settings.shadows.maxDistance);
    w.key("filter");
    w.writeString(shadowFilterToString(settings.shadows.filter));
    w.endObject();

    // lighting
    w.key("lighting");
    w.startObject();
    w.key("maxActiveLights");
    w.writeUint(settings.lighting.maxActiveLights);
    w.key("iblEnabled");
    w.writeBool(settings.lighting.iblEnabled);
    w.endObject();

    // postProcess
    w.key("postProcess");
    w.startObject();

    w.key("ssao");
    w.startObject();
    w.key("enabled");
    w.writeBool(settings.postProcess.ssao.enabled);
    w.key("radius");
    w.writeFloat(settings.postProcess.ssao.radius);
    w.key("bias");
    w.writeFloat(settings.postProcess.ssao.bias);
    w.key("sampleCount");
    w.writeUint(settings.postProcess.ssao.sampleCount);
    w.endObject();

    w.key("bloom");
    w.startObject();
    w.key("enabled");
    w.writeBool(settings.postProcess.bloom.enabled);
    w.key("threshold");
    w.writeFloat(settings.postProcess.bloom.threshold);
    w.key("intensity");
    w.writeFloat(settings.postProcess.bloom.intensity);
    w.key("downsampleSteps");
    w.writeUint(settings.postProcess.bloom.downsampleSteps);
    w.endObject();

    w.key("fxaaEnabled");
    w.writeBool(settings.postProcess.fxaaEnabled);
    w.key("toneMapper");
    w.writeString(toneMapperToString(settings.postProcess.toneMapper));
    w.key("exposure");
    w.writeFloat(settings.postProcess.exposure);

    w.endObject();

    // top-level
    w.key("depthPrepassEnabled");
    w.writeBool(settings.depthPrepassEnabled);
    w.key("depthPrepassAlphaTestedOnly");
    w.writeBool(settings.depthPrepassAlphaTestedOnly);
    w.key("anisotropicFiltering");
    w.writeUint(settings.anisotropicFiltering);
    w.key("renderScale");
    w.writeFloat(settings.renderScale);

    w.endObject();

    return w.writeToFile(filepath);
}

}  // namespace engine::rendering
