#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "engine/assets/AssetManager.h"
#include "engine/assets/IFileSystem.h"
#include "engine/ecs/Registry.h"
#include "engine/io/Json.h"
#include "engine/rendering/EcsComponents.h"
#include "engine/rendering/RenderResources.h"
#include "engine/scene/SceneGraph.h"
#include "engine/scene/SceneSerializer.h"
#include "engine/threading/ThreadPool.h"

using namespace engine;
using namespace engine::ecs;
using namespace engine::rendering;
using namespace engine::scene;

// ---------------------------------------------------------------------------
// Minimal IFileSystem stub — tests use real files via std::tmpnam.
// ---------------------------------------------------------------------------

class StubFileSystem : public assets::IFileSystem
{
public:
    [[nodiscard]] std::vector<uint8_t> read(std::string_view) override
    {
        return {};
    }
    [[nodiscard]] bool exists(std::string_view) override
    {
        return false;
    }
    [[nodiscard]] std::string resolve(std::string_view base, std::string_view rel) override
    {
        return std::string(base) + "/" + std::string(rel);
    }
};

// ---------------------------------------------------------------------------
// RAII temp-file helper — creates a unique path and removes it on destruction.
// ---------------------------------------------------------------------------

struct TempFile
{
    std::string path;

    TempFile()
    {
        auto p = std::filesystem::temp_directory_path() / "nimbus_test_XXXXXX.json";
        path = p.string();
        // Make the path unique by appending the address of this object.
        path += std::to_string(reinterpret_cast<uintptr_t>(this));
    }

    ~TempFile()
    {
        std::remove(path.c_str());
    }
};

// ---------------------------------------------------------------------------
// Helper — create a configured SceneSerializer with engine components.
// ---------------------------------------------------------------------------

static SceneSerializer makeSerializer()
{
    SceneSerializer s;
    s.registerEngineComponents();
    return s;
}

// Float comparison tolerance.
static constexpr float kEps = 1e-5f;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("round-trip empty scene", "[serializer]")
{
    Registry regSave;
    RenderResources resources;
    threading::ThreadPool pool(1);
    StubFileSystem fs;
    assets::AssetManager am(pool, fs);
    TempFile tmp;

    auto serializer = makeSerializer();
    REQUIRE(serializer.saveScene(regSave, resources, tmp.path.c_str()));

    Registry regLoad;
    REQUIRE(serializer.loadScene(tmp.path.c_str(), regLoad, resources, am));

    // No entities with TransformComponent should exist.
    int count = 0;
    regLoad.forEachEntity([&](EntityID) { ++count; });
    REQUIRE(count == 0);
}

TEST_CASE("round-trip single entity with Transform", "[serializer]")
{
    Registry regSave;
    RenderResources resources;
    threading::ThreadPool pool(1);
    StubFileSystem fs;
    assets::AssetManager am(pool, fs);
    TempFile tmp;

    EntityID e = regSave.createEntity();
    TransformComponent tc{};
    tc.position = math::Vec3(1.0f, 2.0f, 3.0f);
    tc.rotation = math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    tc.scale = math::Vec3(1.0f, 1.0f, 1.0f);
    tc.flags = 1;
    regSave.emplace<TransformComponent>(e, tc);

    auto serializer = makeSerializer();
    REQUIRE(serializer.saveScene(regSave, resources, tmp.path.c_str()));

    Registry regLoad;
    REQUIRE(serializer.loadScene(tmp.path.c_str(), regLoad, resources, am));

    int count = 0;
    EntityID loaded = INVALID_ENTITY;
    regLoad.forEachEntity(
        [&](EntityID id)
        {
            ++count;
            loaded = id;
        });
    REQUIRE(count == 1);

    const auto* ltc = regLoad.get<TransformComponent>(loaded);
    REQUIRE(ltc != nullptr);
    REQUIRE(std::abs(ltc->position.x - 1.0f) < kEps);
    REQUIRE(std::abs(ltc->position.y - 2.0f) < kEps);
    REQUIRE(std::abs(ltc->position.z - 3.0f) < kEps);
}

TEST_CASE("round-trip hierarchy", "[serializer]")
{
    Registry regSave;
    RenderResources resources;
    threading::ThreadPool pool(1);
    StubFileSystem fs;
    assets::AssetManager am(pool, fs);
    TempFile tmp;

    EntityID parent = regSave.createEntity();
    EntityID child = regSave.createEntity();

    TransformComponent tcParent{};
    tcParent.position = math::Vec3(0.0f);
    tcParent.rotation = math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    tcParent.scale = math::Vec3(1.0f);
    tcParent.flags = 1;
    regSave.emplace<TransformComponent>(parent, tcParent);

    TransformComponent tcChild{};
    tcChild.position = math::Vec3(5.0f, 0.0f, 0.0f);
    tcChild.rotation = math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    tcChild.scale = math::Vec3(1.0f);
    tcChild.flags = 1;
    regSave.emplace<TransformComponent>(child, tcChild);

    setParent(regSave, child, parent);

    auto serializer = makeSerializer();
    REQUIRE(serializer.saveScene(regSave, resources, tmp.path.c_str()));

    Registry regLoad;
    REQUIRE(serializer.loadScene(tmp.path.c_str(), regLoad, resources, am));

    // Find the two entities.
    std::vector<EntityID> loadedEntities;
    regLoad.forEachEntity([&](EntityID id) { loadedEntities.push_back(id); });
    REQUIRE(loadedEntities.size() == 2);

    // Identify parent (position 0) and child (position 5).
    EntityID loadedParent = INVALID_ENTITY;
    EntityID loadedChild = INVALID_ENTITY;
    for (auto id : loadedEntities)
    {
        const auto* tc = regLoad.get<TransformComponent>(id);
        REQUIRE(tc != nullptr);
        if (std::abs(tc->position.x - 5.0f) < kEps)
            loadedChild = id;
        else
            loadedParent = id;
    }
    REQUIRE(loadedParent != INVALID_ENTITY);
    REQUIRE(loadedChild != INVALID_ENTITY);

    REQUIRE(getParent(regLoad, loadedChild) == loadedParent);
    const auto* children = getChildren(regLoad, loadedParent);
    REQUIRE(children != nullptr);
    REQUIRE(children->size() == 1);
    REQUIRE((*children)[0] == loadedChild);
}

TEST_CASE("round-trip deep hierarchy", "[serializer]")
{
    Registry regSave;
    RenderResources resources;
    threading::ThreadPool pool(1);
    StubFileSystem fs;
    assets::AssetManager am(pool, fs);
    TempFile tmp;

    EntityID a = regSave.createEntity();
    EntityID b = regSave.createEntity();
    EntityID c = regSave.createEntity();

    auto makeTC = [](float x)
    {
        TransformComponent tc{};
        tc.position = math::Vec3(x, 0.0f, 0.0f);
        tc.rotation = math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = math::Vec3(1.0f);
        tc.flags = 1;
        return tc;
    };

    regSave.emplace<TransformComponent>(a, makeTC(10.0f));
    regSave.emplace<TransformComponent>(b, makeTC(20.0f));
    regSave.emplace<TransformComponent>(c, makeTC(30.0f));

    setParent(regSave, b, a);
    setParent(regSave, c, b);

    auto serializer = makeSerializer();
    REQUIRE(serializer.saveScene(regSave, resources, tmp.path.c_str()));

    Registry regLoad;
    REQUIRE(serializer.loadScene(tmp.path.c_str(), regLoad, resources, am));

    // Identify entities by position.
    EntityID la = INVALID_ENTITY, lb = INVALID_ENTITY, lc = INVALID_ENTITY;
    regLoad.forEachEntity(
        [&](EntityID id)
        {
            const auto* tc = regLoad.get<TransformComponent>(id);
            if (!tc)
                return;
            if (std::abs(tc->position.x - 10.0f) < kEps)
                la = id;
            else if (std::abs(tc->position.x - 20.0f) < kEps)
                lb = id;
            else if (std::abs(tc->position.x - 30.0f) < kEps)
                lc = id;
        });

    REQUIRE(la != INVALID_ENTITY);
    REQUIRE(lb != INVALID_ENTITY);
    REQUIRE(lc != INVALID_ENTITY);

    // A is root, B's parent is A, C's parent is B.
    REQUIRE(getParent(regLoad, la) == INVALID_ENTITY);
    REQUIRE(getParent(regLoad, lb) == la);
    REQUIRE(getParent(regLoad, lc) == lb);
}

TEST_CASE("round-trip multiple entities", "[serializer]")
{
    Registry regSave;
    RenderResources resources;
    threading::ThreadPool pool(1);
    StubFileSystem fs;
    assets::AssetManager am(pool, fs);
    TempFile tmp;

    constexpr int kCount = 5;
    for (int i = 0; i < kCount; ++i)
    {
        EntityID e = regSave.createEntity();
        TransformComponent tc{};
        tc.position =
            math::Vec3(static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3));
        tc.rotation = math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        tc.scale = math::Vec3(1.0f);
        tc.flags = 1;
        regSave.emplace<TransformComponent>(e, tc);
    }

    auto serializer = makeSerializer();
    REQUIRE(serializer.saveScene(regSave, resources, tmp.path.c_str()));

    Registry regLoad;
    REQUIRE(serializer.loadScene(tmp.path.c_str(), regLoad, resources, am));

    int count = 0;
    regLoad.forEachEntity([&](EntityID) { ++count; });
    REQUIRE(count == kCount);

    // Verify each position is unique and matches one of the originals.
    std::vector<math::Vec3> positions;
    regLoad.forEachEntity(
        [&](EntityID id)
        {
            const auto* tc = regLoad.get<TransformComponent>(id);
            REQUIRE(tc != nullptr);
            positions.push_back(tc->position);
        });

    for (int i = 0; i < kCount; ++i)
    {
        math::Vec3 expected(static_cast<float>(i), static_cast<float>(i * 2),
                            static_cast<float>(i * 3));
        bool found = false;
        for (const auto& p : positions)
        {
            if (std::abs(p.x - expected.x) < kEps && std::abs(p.y - expected.y) < kEps &&
                std::abs(p.z - expected.z) < kEps)
            {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("entity ordering independence", "[serializer]")
{
    // Manually write JSON where child (id 0) appears before parent (id 1).
    TempFile tmp;

    const char* json = R"({
        "version": 1,
        "entities": [
            {
                "id": 0,
                "parent": 1,
                "components": {
                    "Transform": {
                        "position": [5.0, 0.0, 0.0],
                        "rotation": [0.0, 0.0, 0.0, 1.0],
                        "scale": [1.0, 1.0, 1.0]
                    }
                }
            },
            {
                "id": 1,
                "components": {
                    "Transform": {
                        "position": [0.0, 0.0, 0.0],
                        "rotation": [0.0, 0.0, 0.0, 1.0],
                        "scale": [1.0, 1.0, 1.0]
                    }
                }
            }
        ]
    })";

    // Write JSON to file.
    {
        io::JsonDocument doc;
        REQUIRE(doc.parse(json, std::strlen(json)));
    }
    FILE* f = std::fopen(tmp.path.c_str(), "w");
    REQUIRE(f != nullptr);
    std::fputs(json, f);
    std::fclose(f);

    Registry regLoad;
    RenderResources resources;
    threading::ThreadPool pool(1);
    StubFileSystem fs;
    assets::AssetManager am(pool, fs);

    auto serializer = makeSerializer();
    REQUIRE(serializer.loadScene(tmp.path.c_str(), regLoad, resources, am));

    // Identify child (pos.x == 5) and parent (pos.x == 0).
    EntityID loadedChild = INVALID_ENTITY;
    EntityID loadedParent = INVALID_ENTITY;
    regLoad.forEachEntity(
        [&](EntityID id)
        {
            const auto* tc = regLoad.get<TransformComponent>(id);
            if (!tc)
                return;
            if (std::abs(tc->position.x - 5.0f) < kEps)
                loadedChild = id;
            else
                loadedParent = id;
        });

    REQUIRE(loadedChild != INVALID_ENTITY);
    REQUIRE(loadedParent != INVALID_ENTITY);
    REQUIRE(getParent(regLoad, loadedChild) == loadedParent);
}

TEST_CASE("version field present", "[serializer]")
{
    Registry regSave;
    RenderResources resources;
    threading::ThreadPool pool(1);
    StubFileSystem fs;
    assets::AssetManager am(pool, fs);
    TempFile tmp;

    // Save an empty scene — just ensure version is in the output.
    auto serializer = makeSerializer();
    REQUIRE(serializer.saveScene(regSave, resources, tmp.path.c_str()));

    io::JsonDocument doc;
    REQUIRE(doc.parseFile(tmp.path.c_str()));
    auto root = doc.root();
    REQUIRE(root.hasMember("version"));
    REQUIRE(root["version"].getInt() == 1);
}

TEST_CASE("rotation and scale preserved", "[serializer]")
{
    Registry regSave;
    RenderResources resources;
    threading::ThreadPool pool(1);
    StubFileSystem fs;
    assets::AssetManager am(pool, fs);
    TempFile tmp;

    EntityID e = regSave.createEntity();
    TransformComponent tc{};
    tc.position = math::Vec3(0.0f);
    // Non-identity rotation: 90 degrees around Y axis.
    tc.rotation = math::Quat(0.7071068f, 0.0f, 0.7071068f, 0.0f);  // w, x, y, z in glm order
    tc.scale = math::Vec3(2.0f, 3.0f, 4.0f);
    tc.flags = 1;
    regSave.emplace<TransformComponent>(e, tc);

    auto serializer = makeSerializer();
    REQUIRE(serializer.saveScene(regSave, resources, tmp.path.c_str()));

    Registry regLoad;
    REQUIRE(serializer.loadScene(tmp.path.c_str(), regLoad, resources, am));

    EntityID loaded = INVALID_ENTITY;
    regLoad.forEachEntity([&](EntityID id) { loaded = id; });
    REQUIRE(loaded != INVALID_ENTITY);

    const auto* ltc = regLoad.get<TransformComponent>(loaded);
    REQUIRE(ltc != nullptr);

    // Rotation: glm::quat stores (w, x, y, z).
    REQUIRE(std::abs(ltc->rotation.w - 0.7071068f) < kEps);
    REQUIRE(std::abs(ltc->rotation.x - 0.0f) < kEps);
    REQUIRE(std::abs(ltc->rotation.y - 0.7071068f) < kEps);
    REQUIRE(std::abs(ltc->rotation.z - 0.0f) < kEps);

    // Scale
    REQUIRE(std::abs(ltc->scale.x - 2.0f) < kEps);
    REQUIRE(std::abs(ltc->scale.y - 3.0f) < kEps);
    REQUIRE(std::abs(ltc->scale.z - 4.0f) < kEps);
}
