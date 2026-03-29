#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/io/Json.h"

using namespace engine::io;
using namespace engine::math;

// ---------------------------------------------------------------------------
// 1. Parse valid JSON object
// ---------------------------------------------------------------------------

TEST_CASE("parse valid JSON object", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"key":"value","num":42})"));
    REQUIRE_FALSE(doc.hasError());

    auto root = doc.root();
    REQUIRE(root.isObject());
    CHECK(std::strcmp(root["key"].getString(), "value") == 0);
    CHECK(root["num"].getInt() == 42);
}

// ---------------------------------------------------------------------------
// 2. Parse valid JSON array
// ---------------------------------------------------------------------------

TEST_CASE("parse valid JSON array", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"([1,2,3])"));

    auto root = doc.root();
    REQUIRE(root.isArray());
    CHECK(root.arraySize() == 3);
    CHECK(root[static_cast<std::size_t>(0)].getInt() == 1);
    CHECK(root[static_cast<std::size_t>(1)].getInt() == 2);
    CHECK(root[static_cast<std::size_t>(2)].getInt() == 3);
}

// ---------------------------------------------------------------------------
// 3. Parse failure returns error
// ---------------------------------------------------------------------------

TEST_CASE("parse failure returns error", "[json]")
{
    JsonDocument doc;
    REQUIRE_FALSE(doc.parse("{invalid"));
    CHECK(doc.hasError());
    CHECK(std::strlen(doc.errorMessage()) > 0);
}

// ---------------------------------------------------------------------------
// 4. Typed getters: bool
// ---------------------------------------------------------------------------

TEST_CASE("typed getters: bool", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"v":true})"));

    auto v = doc.root()["v"];
    CHECK(v.isBool());
    CHECK(v.getBool() == true);
}

// ---------------------------------------------------------------------------
// 5. Typed getters: int
// ---------------------------------------------------------------------------

TEST_CASE("typed getters: int", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"v":-42})"));

    auto v = doc.root()["v"];
    CHECK(v.isInt());
    CHECK(v.getInt() == -42);
}

// ---------------------------------------------------------------------------
// 6. Typed getters: uint
// ---------------------------------------------------------------------------

TEST_CASE("typed getters: uint", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"v":100})"));

    auto v = doc.root()["v"];
    CHECK(v.isUint());
    CHECK(v.getUint() == 100);
}

// ---------------------------------------------------------------------------
// 7. Typed getters: float
// ---------------------------------------------------------------------------

TEST_CASE("typed getters: float", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"v":3.14})"));

    auto v = doc.root()["v"];
    CHECK(v.isFloat());
    CHECK(v.getFloat() == Catch::Approx(3.14f));
}

// ---------------------------------------------------------------------------
// 8. Typed getters: string
// ---------------------------------------------------------------------------

TEST_CASE("typed getters: string", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"v":"hello"})"));

    auto v = doc.root()["v"];
    CHECK(v.isString());
    CHECK(std::strcmp(v.getString(), "hello") == 0);
}

// ---------------------------------------------------------------------------
// 9. Default-value getters on type mismatch
// ---------------------------------------------------------------------------

TEST_CASE("default-value getters on type mismatch", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"s":"hello"})"));

    auto s = doc.root()["s"];

    // Asking for int on a string value returns the default.
    CHECK(s.getInt(42) == 42);
    CHECK(s.getBool(true) == true);
    CHECK(s.getUint(7u) == 7u);
    CHECK(s.getFloat(1.5f) == Catch::Approx(1.5f));

    // Asking for string on a missing member returns the default.
    CHECK(std::strcmp(doc.root()["nope"].getString("fallback"), "fallback") == 0);
}

// ---------------------------------------------------------------------------
// 10. Null and missing member
// ---------------------------------------------------------------------------

TEST_CASE("null and missing member", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"n":null})"));

    auto root = doc.root();
    CHECK(root["n"].isNull());
    CHECK(root["nonexistent"].isNull());
}

// ---------------------------------------------------------------------------
// 11. Nested objects
// ---------------------------------------------------------------------------

TEST_CASE("nested objects", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"a":{"b":1}})"));

    CHECK(doc.root()["a"].isObject());
    CHECK(doc.root()["a"]["b"].getInt() == 1);
}

// ---------------------------------------------------------------------------
// 12. Vec3 round-trip
// ---------------------------------------------------------------------------

TEST_CASE("Vec3 round-trip", "[json]")
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

// ---------------------------------------------------------------------------
// 13. Vec4 round-trip
// ---------------------------------------------------------------------------

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
// 14. Quat round-trip
// ---------------------------------------------------------------------------

TEST_CASE("Quat round-trip", "[json]")
{
    // glm::quat constructor: (w, x, y, z)
    Quat original(0.707f, 0.0f, 0.707f, 0.0f);

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

// ---------------------------------------------------------------------------
// 15. Object iteration
// ---------------------------------------------------------------------------

TEST_CASE("object iteration", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"a":1,"b":2})"));

    std::vector<std::string> keys;
    std::vector<int> values;

    for (auto member : doc.root())
    {
        REQUIRE(member.memberName() != nullptr);
        keys.emplace_back(member.memberName());
        values.push_back(member.getInt());
    }

    REQUIRE(keys.size() == 2);
    CHECK(keys[0] == "a");
    CHECK(keys[1] == "b");
    CHECK(values[0] == 1);
    CHECK(values[1] == 2);
}

// ---------------------------------------------------------------------------
// 16. Array iteration
// ---------------------------------------------------------------------------

TEST_CASE("array iteration", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"([10,20,30])"));

    std::vector<int> values;
    for (auto elem : doc.root())
    {
        values.push_back(elem.getInt());
    }

    REQUIRE(values.size() == 3);
    CHECK(values[0] == 10);
    CHECK(values[1] == 20);
    CHECK(values[2] == 30);
}

// ---------------------------------------------------------------------------
// 17. Writer produces valid JSON
// ---------------------------------------------------------------------------

TEST_CASE("writer produces valid JSON", "[json]")
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
    REQUIRE(root["values"].isArray());
    CHECK(root["values"].arraySize() == 2);
    CHECK(root["values"][static_cast<std::size_t>(0)].getFloat() == Catch::Approx(1.0f));
    CHECK(root["values"][static_cast<std::size_t>(1)].getFloat() == Catch::Approx(2.0f));
}

// ---------------------------------------------------------------------------
// 18. Writer file I/O
// ---------------------------------------------------------------------------

TEST_CASE("writer file I/O", "[json]")
{
    const char* path = "/tmp/test_json_write.json";

    JsonWriter writer(true);
    writer.startObject();
    writer.key("msg");
    writer.writeString("file test");
    writer.key("number");
    writer.writeInt(99);
    writer.endObject();

    REQUIRE(writer.writeToFile(path));

    JsonDocument doc;
    REQUIRE(doc.parseFile(path));

    auto root = doc.root();
    CHECK(std::strcmp(root["msg"].getString(), "file test") == 0);
    CHECK(root["number"].getInt() == 99);

    std::remove(path);
}

// ---------------------------------------------------------------------------
// 19. Empty document
// ---------------------------------------------------------------------------

TEST_CASE("empty document", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse("{}"));

    auto root = doc.root();
    CHECK(root.isObject());
    CHECK_FALSE(root.hasMember("anything"));

    // Iteration over an empty object yields nothing.
    int count = 0;
    for ([[maybe_unused]] auto member : root)
    {
        count++;
    }
    CHECK(count == 0);
}

// ---------------------------------------------------------------------------
// 20. hasMember
// ---------------------------------------------------------------------------

TEST_CASE("hasMember", "[json]")
{
    JsonDocument doc;
    REQUIRE(doc.parse(R"({"key":"value","num":42})"));

    CHECK(doc.root().hasMember("key"));
    CHECK(doc.root().hasMember("num"));
    CHECK_FALSE(doc.root().hasMember("missing"));
    CHECK_FALSE(doc.root().hasMember(""));
}

// ---------------------------------------------------------------------------
// Bonus: writeNull round-trip
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

// ---------------------------------------------------------------------------
// Bonus: Vec2 round-trip
// ---------------------------------------------------------------------------

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
