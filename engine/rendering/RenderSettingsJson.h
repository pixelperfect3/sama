#pragma once

#include "engine/rendering/GpuFeatures.h"
#include "engine/rendering/RenderSettings.h"

namespace engine::rendering
{

/// Load RenderSettings from a JSON config file. Missing fields use the
/// platform default (from renderSettingsPlatformDefault).
RenderSettings loadRenderSettings(const char* filepath, const GpuFeatures& gpu);

/// Write current settings to JSON (pretty-printed, for user config persistence).
bool saveRenderSettings(const RenderSettings& settings, const char* filepath);

}  // namespace engine::rendering
