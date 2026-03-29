#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <deque>

#include "engine/input/ActionMapJson.h"

using namespace engine::input;

// Helper: write a string to a temp file and return the path.
static std::string writeTempJson(const char* json)
{
    std::string path = std::string(ENGINE_SOURCE_DIR) + "/tests/input/_tmp_bindings.json";
    FILE* f = std::fopen(path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(json, f);
    std::fclose(f);
    return path;
}

static void removeTempFile(const std::string& path)
{
    std::remove(path.c_str());
}

TEST_CASE("ActionMapJson: round-trip key bindings", "[config]")
{
    ActionMap original;
    original.bindKey(Key::W, "MoveForward");
    original.bindKey(Key::S, "MoveBack");
    original.bindKey(Key::Space, "Jump");
    original.bindKey(Key::Escape, "Pause");

    std::string path = std::string(ENGINE_SOURCE_DIR) + "/tests/input/_tmp_keys_rt.json";
    REQUIRE(saveActionMap(original, path.c_str()));

    ActionMap loaded;
    std::deque<std::string> owned;
    REQUIRE(loadActionMap(path.c_str(), loaded, owned));

    CHECK(loaded.keyAction(Key::W) == "MoveForward");
    CHECK(loaded.keyAction(Key::S) == "MoveBack");
    CHECK(loaded.keyAction(Key::Space) == "Jump");
    CHECK(loaded.keyAction(Key::Escape) == "Pause");

    removeTempFile(path);
}

TEST_CASE("ActionMapJson: round-trip mouse bindings", "[config]")
{
    ActionMap original;
    original.bindMouseButton(MouseButton::Left, "Attack");
    original.bindMouseButton(MouseButton::Right, "Block");

    std::string path = std::string(ENGINE_SOURCE_DIR) + "/tests/input/_tmp_mouse_rt.json";
    REQUIRE(saveActionMap(original, path.c_str()));

    ActionMap loaded;
    std::deque<std::string> owned;
    REQUIRE(loadActionMap(path.c_str(), loaded, owned));

    CHECK(loaded.mouseButtonAction(MouseButton::Left) == "Attack");
    CHECK(loaded.mouseButtonAction(MouseButton::Right) == "Block");

    removeTempFile(path);
}

TEST_CASE("ActionMapJson: round-trip axis bindings", "[config]")
{
    ActionMap original;
    original.bindAxis("MoveX", Key::A, Key::D);
    original.bindAxis("MoveY", Key::S, Key::W);

    std::string path = std::string(ENGINE_SOURCE_DIR) + "/tests/input/_tmp_axis_rt.json";
    REQUIRE(saveActionMap(original, path.c_str()));

    ActionMap loaded;
    std::deque<std::string> owned;
    REQUIRE(loadActionMap(path.c_str(), loaded, owned));

    auto* moveX = loaded.axisBinding("MoveX");
    REQUIRE(moveX != nullptr);
    CHECK(moveX->negative == Key::A);
    CHECK(moveX->positive == Key::D);

    auto* moveY = loaded.axisBinding("MoveY");
    REQUIRE(moveY != nullptr);
    CHECK(moveY->negative == Key::S);
    CHECK(moveY->positive == Key::W);

    removeTempFile(path);
}

TEST_CASE("ActionMapJson: unknown key name ignored", "[config]")
{
    std::string path = writeTempJson(R"({
        "keys": {
            "FakeKey": "SomeAction",
            "W": "MoveForward"
        },
        "mouseButtons": {},
        "axes": []
    })");

    ActionMap loaded;
    std::deque<std::string> owned;
    REQUIRE(loadActionMap(path.c_str(), loaded, owned));

    CHECK(loaded.keyAction(Key::W) == "MoveForward");

    removeTempFile(path);
}

TEST_CASE("ActionMapJson: empty file produces empty map", "[config]")
{
    std::string path = writeTempJson(R"({})");

    ActionMap loaded;
    std::deque<std::string> owned;
    REQUIRE(loadActionMap(path.c_str(), loaded, owned));

    CHECK(loaded.keyAction(Key::W) == "");
    CHECK(loaded.mouseButtonAction(MouseButton::Left) == "");

    removeTempFile(path);
}
