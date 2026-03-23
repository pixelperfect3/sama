#pragma once

#include <cstdint>

namespace engine::rendering
{

struct RendererDesc
{
    void* nativeWindowHandle;
    void* nativeDisplayHandle;
    uint32_t width;
    uint32_t height;
    bool headless;  // if true, use RendererType::Noop (for unit tests)
};

class Renderer
{
public:
    bool init(const RendererDesc& desc);
    void beginFrame();
    void endFrame();  // calls bgfx::frame()
    void shutdown();
    void resize(uint32_t w, uint32_t h);
    bool isHeadless() const;

private:
    bool initialized_ = false;
    bool headless_ = false;
};

}  // namespace engine::rendering
