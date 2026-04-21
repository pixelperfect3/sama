#pragma once

#include <android/asset_manager.h>

namespace engine::platform
{

/// Store the AAssetManager pointer for global access (called during Engine::initAndroid).
void setAssetManager(AAssetManager* am);

/// Retrieve the stored AAssetManager pointer (used by ShaderLoader on Android).
AAssetManager* getAssetManager();

}  // namespace engine::platform
