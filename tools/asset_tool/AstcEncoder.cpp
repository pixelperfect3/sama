#include "tools/asset_tool/AstcEncoder.h"

#include <astcenc.h>

#include <iostream>

namespace engine::tools
{

namespace
{

bool astcencCompress(const uint8_t* pixels, int width, int height, int blockX, int blockY,
                     std::vector<uint8_t>& output)
{
    astcenc_config config{};
    astcenc_error status =
        astcenc_config_init(ASTCENC_PRF_LDR, static_cast<unsigned int>(blockX),
                            static_cast<unsigned int>(blockY), 1, ASTCENC_PRE_MEDIUM, 0, &config);

    if (status != ASTCENC_SUCCESS)
    {
        std::cerr << "  astcenc config init failed: " << astcenc_get_error_string(status) << "\n";
        return false;
    }

    astcenc_context* context = nullptr;
    status = astcenc_context_alloc(&config, 1, &context);
    if (status != ASTCENC_SUCCESS)
    {
        std::cerr << "  astcenc context alloc failed: " << astcenc_get_error_string(status) << "\n";
        return false;
    }

    astcenc_image image{};
    image.dim_x = static_cast<unsigned int>(width);
    image.dim_y = static_cast<unsigned int>(height);
    image.dim_z = 1;
    image.data_type = ASTCENC_TYPE_U8;

    uint8_t* slicePtr = const_cast<uint8_t*>(pixels);
    void* slices[1] = {slicePtr};
    image.data = slices;

    int blocksX = (width + blockX - 1) / blockX;
    int blocksY = (height + blockY - 1) / blockY;
    size_t compressedSize = static_cast<size_t>(blocksX) * static_cast<size_t>(blocksY) * 16;
    output.resize(compressedSize);

    const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};

    status = astcenc_compress_image(context, &image, &swizzle, output.data(), compressedSize, 0);

    astcenc_context_free(context);

    if (status != ASTCENC_SUCCESS)
    {
        std::cerr << "  astcenc compress failed: " << astcenc_get_error_string(status) << "\n";
        output.clear();
        return false;
    }

    return true;
}

// Self-registering initializer: when this object file is linked, it registers
// the real astcenc compressor at static-init time.
struct AstcencRegistrar
{
    AstcencRegistrar()
    {
        registerAstcCompressor(astcencCompress);
    }
};

static AstcencRegistrar s_registrar;

}  // namespace

}  // namespace engine::tools
