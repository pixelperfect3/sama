#include "tools/asset_tool/AstcEncoder.h"

namespace engine::tools
{

namespace
{
AstcCompressFn g_compressor = nullptr;
}  // namespace

void registerAstcCompressor(AstcCompressFn fn)
{
    g_compressor = fn;
}

bool compressAstc(const uint8_t* pixels, int width, int height, int blockX, int blockY,
                  std::vector<uint8_t>& output)
{
    if (!g_compressor)
        return false;
    return g_compressor(pixels, width, height, blockX, blockY, output);
}

bool isAstcEncoderAvailable()
{
    return g_compressor != nullptr;
}

}  // namespace engine::tools
