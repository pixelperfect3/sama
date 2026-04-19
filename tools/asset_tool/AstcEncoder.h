#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::tools
{

/// Function type for ASTC compression.
using AstcCompressFn = bool (*)(const uint8_t* pixels, int width, int height, int blockX,
                                int blockY, std::vector<uint8_t>& output);

/// Register an ASTC compression function. Called at static-init time by the
/// bridge object library when it is linked in.
void registerAstcCompressor(AstcCompressFn fn);

/// Compress RGBA pixel data to ASTC format.
/// Returns false if no compressor has been registered.
bool compressAstc(const uint8_t* pixels, int width, int height, int blockX, int blockY,
                  std::vector<uint8_t>& output);

/// Check whether ASTC encoding support is available.
bool isAstcEncoderAvailable();

}  // namespace engine::tools
