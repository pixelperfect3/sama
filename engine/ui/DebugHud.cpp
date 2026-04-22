#include "engine/ui/DebugHud.h"

#include <cstdarg>
#include <cstdio>

#include "engine/math/Types.h"
#include "engine/rendering/ViewIds.h"
#include "engine/ui/DefaultFont.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiRenderer.h"

namespace engine::ui
{

static constexpr float kCellWidth = 8.0f;
static constexpr float kCellHeight = 16.0f;
static constexpr float kFontSize = 16.0f;

DebugHud::DebugHud() = default;

DebugHud::~DebugHud()
{
    shutdown();
}

void DebugHud::init()
{
    renderer_ = new UiRenderer();
    renderer_->init();

    drawList_ = new UiDrawList();

    font_ = defaultFont();
}

void DebugHud::shutdown()
{
    if (renderer_)
    {
        renderer_->shutdown();
        delete renderer_;
        renderer_ = nullptr;
    }

    delete drawList_;
    drawList_ = nullptr;

    font_ = nullptr;
}

void DebugHud::begin(uint32_t fbWidth, uint32_t fbHeight)
{
    fbWidth_ = fbWidth;
    fbHeight_ = fbHeight;
    if (drawList_)
    {
        drawList_->clear();
    }
}

void DebugHud::printf(uint16_t col, uint16_t row, uint32_t color, const char* fmt, ...)
{
    if (!drawList_ || !font_)
    {
        return;
    }

    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Convert packed RGBA uint32_t to glm::vec4 (0-1 range).
    float r = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    float g = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    float b = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    float a = static_cast<float>((color >> 0) & 0xFF) / 255.0f;

    math::Vec2 pos{col * kCellWidth, row * kCellHeight};
    math::Vec4 rgba{r, g, b, a};

    drawList_->drawText(pos, buf, rgba, font_, kFontSize);
}

void DebugHud::printf(uint16_t col, uint16_t row, const char* fmt, ...)
{
    if (!drawList_ || !font_)
    {
        return;
    }

    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    math::Vec2 pos{col * kCellWidth, row * kCellHeight};
    math::Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};

    drawList_->drawText(pos, buf, white, font_, kFontSize);
}

void DebugHud::end()
{
    if (!renderer_ || !drawList_)
    {
        return;
    }

    renderer_->render(*drawList_, rendering::kViewDebugHud, static_cast<uint16_t>(fbWidth_),
                      static_cast<uint16_t>(fbHeight_));
}

}  // namespace engine::ui
