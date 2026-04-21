#include "engine/platform/android/AndroidGlobals.h"

namespace engine::platform
{

static AAssetManager* g_assetManager = nullptr;

void setAssetManager(AAssetManager* am)
{
    g_assetManager = am;
}

AAssetManager* getAssetManager()
{
    return g_assetManager;
}

}  // namespace engine::platform
