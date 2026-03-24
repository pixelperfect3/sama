#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace engine::screenshot
{

// Set by main.cpp when --update-goldens is passed on the command line.
extern bool gUpdateGoldens;

// Golden directory path baked in at compile time via ENGINE_SOURCE_DIR.
std::string goldenPath(const std::string& name);

// Compare pixels against the stored golden for `name`.
//   - If the golden file does not exist yet: save it and return true (first-run auto-creates).
//   - If gUpdateGoldens is true: overwrite the golden and return true.
//   - Otherwise: load the golden, compare RGBA channels pixel-by-pixel.
//     Return true if <= 1% of pixels have a max per-channel delta > tolerance.
// tolerance defaults to 8 (loose; tolerant of minor GPU-driver differences).
bool compareOrUpdateGolden(const std::string& name, const std::vector<uint8_t>& pixels,
                           uint16_t width, uint16_t height, uint8_t tolerance = 8);

}  // namespace engine::screenshot
