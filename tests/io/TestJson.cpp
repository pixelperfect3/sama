#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>

#include "engine/io/Json.h"

using namespace engine::io;
using namespace engine::math;

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

TEST_CASE("Parse valid JSON object", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"key": "value"})"));

    auto root = doc.root();
    REQUIRE(root.isObject());
    CHECK(std::strcmp(root["key"].getString(), "value") == 0);
}

TEST_CASE("Parse valid JSON array", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"([1, 2, 3])"));

    auto root = doc.root();
    REQUIRE(root.isArray());
    CHECK(root.arraySize() == 3);
    CHECK(root[static_cast<std::size_t>(0)].getInt() == 1);
    CHECK(root[static_cast<std::size_t>(1)].getInt() == 2);
    CHECK(root[static_cast<std::size_t>(2)].getInt() == 3);
}

TEST_CASE("Parse failure returns error", "[json]")
{
    JsonDocument doc;
    REQUIRE_FALSE(doc.parse("{invalid"));
    CHECK(doc.hasError());
    CHECK(std::strlen(doc.errorMessage()) > 0);
}

// ---------------------------------------------------------------------------
// Typed getters
// ---------------------------------------------------------------------------

TEST_CASE("Typed getters: bool, int, uint, float, string", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({
        "b": true,
        "i": -42,
        "u": 100,
        "f": 3.14,
        "s": "hello"
    })"));

    auto root = doc.root();
    CHECK(root["b"].isBool());
    CHECK(root["b"].getBool() == true);

    CHECK(root["i"].isInt());
    CHECK(root["i"].getInt() == -42);

    CHECK(root["u"].isUint());
    CHECK(root["u"].getUint() == 100);

    CHECK(root["f"].isFloat());
    CHECK(root["f"].getFloat() == Catch::Approx(3.14f));

    CHECK(root["s"].isString());
    CHECK(std::strcmp(root["s"].getString(), "hello") == 0);
}

// ---------------------------------------------------------------------------
// Default-value getters on type mismatch
// ---------------------------------------------------------------------------

TEST_CASE("Default-value getters on type mismatch", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"s": "hello"})"));

    auto root = doc.root();

    // Asking for int on a string value returns the default.
    CHECK(root["s"].getInt(42) == 42);
    CHECK(root["s"].getBool(true) == true);
    CHECK(root["s"].getUint(7u) == 7u);
    CHECK(root["s"].getFloat(1.5f) == 1.5f);

    // Asking for string on a missing member returns the default.
    CHECK(std::strcmp(root["nope"].getString("fallback"), "fallback") == 0);
}

// ---------------------------------------------------------------------------
// Null and missing member
// ---------------------------------------------------------------------------

TEST_CASE("Null and missing member", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"n": null})"));

    auto root = doc.root();
    CHECK(root["n"].isNull());
    CHECK(root["nonexistent"].isNull());
}

// ---------------------------------------------------------------------------
// Math round-trips
// ---------------------------------------------------------------------------

TEST_CASE("Vec3 round-trip (write then parse)", "[json]")
{
    Vec3 original(1.0f, 2.0f, 3.0f);

    JsonWriter writer(false);
    writer.writeVec3(original);

    JsonDocument doc;
    REQUIRE(doc.parse(writer.getString()));

    Vec3 loaded = doc.root().getVec3();
    CHECK(loaded.x == Catch::Approx(original.x));
    CHECK(loaded.y == Catch::Approx(original.y));
    CHECK(loaded.z == Catch::Approx(original.z));
}

TEST_CASE("Quat round-trip (write then parse)", "[json]")
{
    Quat original(0.707f, 0.0f, 0.707f, 0.0f);  // w, x, y, z

    JsonWriter writer(false);
    writer.writeQuat(original);

    JsonDocument doc;
    REQUIRE(doc.parse(writer.getString()));

    Quat loaded = doc.root().getQuat();
    CHECK(loaded.x == Catch::Approx(original.x));
    CHECK(loaded.y == Catch::Approx(original.y));
    CHECK(loaded.z == Catch::Approx(original.z));
    CHECK(loaded.w == Catch::Approx(original.w));
}

TEST_CASE("Vec2 round-trip", "[json]")
{
    Vec2 original(5.5f, -3.2f);

    JsonWriter writer(false);
    writer.writeVec2(original);

    JsonDocument doc;
    REQUIRE(doc.parse(writer.getString()));

    Vec2 loaded = doc.root().getVec2();
    CHECK(loaded.x == Catch::Approx(original.x));
    CHECK(loaded.y == Catch::Approx(original.y));
}

TEST_CASE("Vec4 round-trip", "[json]")
{
    Vec4 original(0.1f, 0.2f, 0.3f, 0.4f);

    JsonWriter writer(false);
    writer.writeVec4(original);

    JsonDocument doc;
    REQUIRE(doc.parse(writer.getString()));

    Vec4 loaded = doc.root().getVec4();
    CHECK(loaded.x == Catch::Approx(original.x));
    CHECK(loaded.y == Catch::Approx(original.y));
    CHECK(loaded.z == Catch::Approx(original.z));
    CHECK(loaded.w == Catch::Approx(original.w));
}

// ---------------------------------------------------------------------------
// Object and array iteration
// ---------------------------------------------------------------------------

TEST_CASE("Object iteration", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"a": 1, "b": 2, "c": 3})"));

    int count = 0;
    for (auto member : doc.root())
    {
        REQUIRE(member.memberName() != nullptr);
        CHECK(member.isInt());
        count++;
    }
    CHECK(count == 3);
}

TEST_CASE("Array iteration", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"([10, 20, 30])"));

    int sum = 0;
    int count = 0;
    for (auto elem : doc.root())
    {
        sum += elem.getInt();
        count++;
    }
    CHECK(count == 3);
    CHECK(sum == 60);
}

// ---------------------------------------------------------------------------
// Writer produces valid JSON
// ---------------------------------------------------------------------------

TEST_CASE("Writer produces valid JSON", "[json]")
{
    JsonWriter writer(false);
    writer.startObject();
    writer.key("name");
    writer.writeString("test");
    writer.key("count");
    writer.writeInt(42);
    writer.key("enabled");
    writer.writeBool(true);
    writer.key("values");
    writer.startArray();
    writer.writeFloat(1.0f);
    writer.writeFloat(2.0f);
    writer.endArray();
    writer.endObject();

    JsonDocument doc;
    REQUIRE(doc.parse(writer.getString()));

    auto root = doc.root();
    CHECK(std::strcmp(root["name"].getString(), "test") == 0);
    CHECK(root["count"].getInt() == 42);
    CHECK(root["enabled"].getBool() == true);
    CHECK(root["values"].arraySize() == 2);
}

// ---------------------------------------------------------------------------
// Writer file I/O
// ---------------------------------------------------------------------------

TEST_CASE("Writer file I/O", "[json]")
{
    const char* path = "/tmp/nimbus_test_json.json";

    JsonWriter writer(true);
    writer.startObject();
    writer.key("msg");
    writer.writeString("file test");
    writer.endObject();

    REQUIRE(writer.writeToFile(path));

    JsonDocument doc;
    REQUIRE(doc.parseFile(path));

    auto root = doc.root();
    CHECK(std::strcmp(root["msg"].getString(), "file test") == 0);

    std::remove(path);
}

// ---------------------------------------------------------------------------
// Nested objects
// ---------------------------------------------------------------------------

TEST_CASE("Nested objects", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"a": {"b": 1}})"));

    CHECK(doc.root()["a"]["b"].getInt() == 1);
}

// ---------------------------------------------------------------------------
// Empty document
// ---------------------------------------------------------------------------

TEST_CASE("Empty document", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse("{}"));

    auto root = doc.root();
    CHECK(root.isObject());
    CHECK_FALSE(root.hasMember("anything"));
}

// ---------------------------------------------------------------------------
// hasMember
// ---------------------------------------------------------------------------

TEST_CASE("hasMember", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"exists": 1})"));

    CHECK(doc.root().hasMember("exists"));
    CHECK_FALSE(doc.root().hasMember("nope"));
}

// ---------------------------------------------------------------------------
// writeNull
// ---------------------------------------------------------------------------

TEST_CASE("writeNull round-trip", "[json]")
{
    JsonWriter writer(false);
    writer.startObject();
    writer.key("n");
    writer.writeNull();
    writer.endObject();

    JsonDocument doc;
    REQUIRE(doc.parse(writer.getString()));
    CHECK(doc.root()["n"].isNull());
}
