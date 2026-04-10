#include "engine/ui/DefaultFont.h"

#include <memory>

#include "engine/ui/BitmapFont.h"

namespace engine::ui
{

namespace
{
std::unique_ptr<BitmapFont> g_defaultFont;
}  // namespace

IFont* defaultFont()
{
    if (!g_defaultFont)
    {
        g_defaultFont = std::make_unique<BitmapFont>();
        g_defaultFont->createDebugFont();
    }
    return g_defaultFont.get();
}

void shutdownDefaultFont()
{
    if (g_defaultFont)
    {
        g_defaultFont->shutdown();
        g_defaultFont.reset();
    }
}

}  // namespace engine::ui
