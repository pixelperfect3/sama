#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>

namespace engine::assets
{

// ---------------------------------------------------------------------------
// Texture — live bgfx GPU texture handle plus metadata.
//
// Owned by the AssetManager payload. Destroyed when the asset is released.
// ---------------------------------------------------------------------------

struct Texture
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool isValid() const
    {
        return bgfx::isValid(handle);
    }

    void destroy()
    {
        if (bgfx::isValid(handle))
            bgfx::destroy(handle);
        *this = Texture{};
    }
};

}  // namespace engine::assets
