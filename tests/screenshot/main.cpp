#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <string>
#include <vector>

#include "GoldenCompare.h"
#include "ScreenshotFixture.h"

int main(int argc, char* argv[])
{
    // Strip --update-goldens before handing args to Catch2 (it would reject it as unknown).
    std::vector<char*> args;
    for (int i = 0; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--update-goldens")
            engine::screenshot::gUpdateGoldens = true;
        else
            args.push_back(argv[i]);
    }

    // Initialize bgfx once for the entire test process.
    // All ScreenshotFixture instances (per-test render targets) share this context.
    engine::screenshot::BgfxContext bgfxCtx;

    return Catch::Session().run(static_cast<int>(args.size()), args.data());
}
