#include "engine/platform/ios/IosGlobals.h"

#if defined(__APPLE__) && TARGET_OS_IPHONE

namespace engine::platform::ios
{

namespace
{
// Owned by the iOS runtime (NSBundle's resourcePath).  We do not free it.
const char* g_resourcePath = nullptr;

// Non-owning UIView* stored as void*.  The view's lifetime is managed by the
// application (typically the IosWindow's view controller).  We never retain
// or release through this pointer.
void* g_gameView = nullptr;
}  // namespace

void setResourceBundlePath(const char* path)
{
    g_resourcePath = path;
}

const char* getResourceBundlePath()
{
    return g_resourcePath;
}

void setGameView(void* uiViewPtr)
{
    g_gameView = uiViewPtr;
}

void* getGameView()
{
    return g_gameView;
}

}  // namespace engine::platform::ios

#else  // not iOS — provide stubs so this file compiles on macOS desktop.

namespace engine::platform::ios
{

void setResourceBundlePath(const char*) {}
const char* getResourceBundlePath()
{
    return nullptr;
}
void setGameView(void*) {}
void* getGameView()
{
    return nullptr;
}

}  // namespace engine::platform::ios

#endif  // __APPLE__ && TARGET_OS_IPHONE
