// bake_default_env — generate the procedural sky asset and write it to disk.
//
// Run once to produce assets/env/default.env, the binary cubemap+BRDF LUT
// blob shipped with the editor. Subsequent editor launches load this file
// instead of regenerating, dropping startup time from ~5 s to <50 ms.
//
// Usage (from the repo root):
//   cmake --build build --target bake_default_env -j$(sysctl -n hw.ncpu)
//   ./build/tools/bake_default_env [output_path]
//
// If `output_path` is omitted, defaults to assets/env/default.env relative
// to cwd. The file is overwritten if it exists.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#include "engine/assets/EnvironmentAssetSerializer.h"
#include "engine/rendering/IblResources.h"

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    using clock = std::chrono::steady_clock;

    const std::string outPath = (argc >= 2) ? argv[1] : "assets/env/default.env";

    fs::path parent = fs::path(outPath).parent_path();
    if (!parent.empty())
    {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec)
        {
            std::fprintf(stderr, "bake_default_env: cannot create %s: %s\n",
                         parent.string().c_str(), ec.message().c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "bake_default_env: generating procedural sky asset...\n");
    const auto t0 = clock::now();
    const auto env = engine::rendering::IblResources::generateDefaultAsset();
    const auto t1 = clock::now();
    const double genMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::fprintf(stderr, "bake_default_env: CPU generation took %.0f ms\n", genMs);

    std::fprintf(stderr, "bake_default_env: writing %s ...\n", outPath.c_str());
    const auto t2 = clock::now();
    if (!engine::assets::saveEnvironmentAsset(outPath, env))
    {
        std::fprintf(stderr, "bake_default_env: save failed\n");
        return 2;
    }
    const auto t3 = clock::now();
    const double saveMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

    const auto fileBytes = fs::file_size(outPath);
    std::fprintf(stderr,
                 "bake_default_env: wrote %.2f MB in %.0f ms\n"
                 "bake_default_env: total %.0f ms\n",
                 static_cast<double>(fileBytes) / (1024.0 * 1024.0), saveMs,
                 std::chrono::duration<double, std::milli>(t3 - t0).count());

    return 0;
}
