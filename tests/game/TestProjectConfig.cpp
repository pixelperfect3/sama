#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/core/Engine.h"
#include "engine/game/ProjectConfig.h"

using namespace engine::game;
using Catch::Matchers::WithinAbs;

TEST_CASE("ProjectConfig defaults", "[game][config]")
{
    ProjectConfig config;

    CHECK(config.name == "Untitled");
    CHECK(config.startupScene.empty());
    CHECK(config.window.title == "Sama");
    CHECK(config.window.width == 1280);
    CHECK(config.window.height == 720);
    CHECK(config.window.fullscreen == false);
    CHECK(config.render.shadowResolution == 2048);
    CHECK(config.render.shadowCascades == 1);
    CHECK_THAT(config.physics.fixedTimestep, WithinAbs(1.0f / 60.0f, 1e-6f));
    CHECK_THAT(config.physics.gravity[1], WithinAbs(-9.81f, 1e-3f));
    CHECK_THAT(config.audio.masterVolume, WithinAbs(1.0f, 1e-6f));
    CHECK(config.frameArenaSize == 2 * 1024 * 1024);
}

TEST_CASE("ProjectConfig loadFromString — full config", "[game][config]")
{
    const char* json = R"({
        "name": "Test Game",
        "startupScene": "scenes/test.json",
        "window": {
            "title": "Test Window",
            "width": 1920,
            "height": 1080,
            "fullscreen": true
        },
        "render": {
            "shadowResolution": 4096,
            "shadowCascades": 3
        },
        "physics": {
            "fixedTimestep": 0.008333,
            "gravity": [0, -20.0, 0],
            "maxSubSteps": 8
        },
        "audio": {
            "masterVolume": 0.8,
            "musicVolume": 0.5,
            "sfxVolume": 0.9
        },
        "frameArenaSize": 4194304
    })";

    ProjectConfig config;
    REQUIRE(config.loadFromString(json, strlen(json)));

    CHECK(config.name == "Test Game");
    CHECK(config.startupScene == "scenes/test.json");
    CHECK(config.window.title == "Test Window");
    CHECK(config.window.width == 1920);
    CHECK(config.window.height == 1080);
    CHECK(config.window.fullscreen == true);
    CHECK(config.render.shadowResolution == 4096);
    CHECK(config.render.shadowCascades == 3);
    CHECK_THAT(config.physics.fixedTimestep, WithinAbs(0.008333f, 1e-4f));
    CHECK_THAT(config.physics.gravity[1], WithinAbs(-20.0f, 1e-3f));
    CHECK(config.physics.maxSubSteps == 8);
    CHECK_THAT(config.audio.masterVolume, WithinAbs(0.8f, 1e-3f));
    CHECK_THAT(config.audio.musicVolume, WithinAbs(0.5f, 1e-3f));
    CHECK_THAT(config.audio.sfxVolume, WithinAbs(0.9f, 1e-3f));
    CHECK(config.frameArenaSize == 4194304);
}

TEST_CASE("ProjectConfig loadFromString — partial config keeps defaults", "[game][config]")
{
    const char* json = R"({
        "window": {
            "title": "Partial"
        }
    })";

    ProjectConfig config;
    REQUIRE(config.loadFromString(json, strlen(json)));

    CHECK(config.window.title == "Partial");
    CHECK(config.window.width == 1280);
    CHECK(config.window.height == 720);
    CHECK(config.render.shadowResolution == 2048);
    CHECK_THAT(config.physics.fixedTimestep, WithinAbs(1.0f / 60.0f, 1e-6f));
}

TEST_CASE("ProjectConfig loadFromString — fixedRateHz", "[game][config]")
{
    const char* json = R"({
        "physics": {
            "fixedRateHz": 30
        }
    })";

    ProjectConfig config;
    REQUIRE(config.loadFromString(json, strlen(json)));

    CHECK_THAT(config.physics.fixedTimestep, WithinAbs(1.0f / 30.0f, 1e-6f));
}

TEST_CASE("ProjectConfig loadFromString — empty object", "[game][config]")
{
    const char* json = "{}";

    ProjectConfig config;
    REQUIRE(config.loadFromString(json, strlen(json)));

    CHECK(config.name == "Untitled");
    CHECK(config.window.title == "Sama");
}

TEST_CASE("ProjectConfig loadFromString — invalid JSON", "[game][config]")
{
    const char* json = "not json at all";

    ProjectConfig config;
    CHECK_FALSE(config.loadFromString(json, strlen(json)));
}

TEST_CASE("ProjectConfig toEngineDesc", "[game][config]")
{
    ProjectConfig config;
    config.window.title = "Test";
    config.window.width = 800;
    config.window.height = 600;
    config.render.shadowResolution = 1024;
    config.render.shadowCascades = 2;
    config.frameArenaSize = 1024 * 1024;

    auto desc = config.toEngineDesc();
    CHECK(desc.windowWidth == 800);
    CHECK(desc.windowHeight == 600);
    CHECK(desc.shadowResolution == 1024);
    CHECK(desc.shadowCascades == 2);
    CHECK(desc.frameArenaSize == 1024 * 1024);
}

TEST_CASE("ProjectConfig loadFromFile — missing file", "[game][config]")
{
    ProjectConfig config;
    CHECK_FALSE(config.loadFromFile("/nonexistent/path/project.json"));
    // Defaults should be unchanged.
    CHECK(config.window.title == "Sama");
}
