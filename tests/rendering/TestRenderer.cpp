#include <catch2/catch_test_macros.hpp>

#include "engine/rendering/Renderer.h"

TEST_CASE("Renderer: headless init and shutdown", "[renderer]")
{
    engine::rendering::Renderer r;
    engine::rendering::RendererDesc desc{};
    desc.headless = true;
    desc.width = 1280;
    desc.height = 720;
    REQUIRE(r.init(desc));
    r.beginFrame();
    r.endFrame();
    r.shutdown();
}
