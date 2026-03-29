#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "engine/assets/CpuAssetData.h"
#include "engine/assets/IFileSystem.h"
#include "engine/assets/ObjLoader.h"

using namespace engine::assets;

// ---------------------------------------------------------------------------
// Minimal in-memory IFileSystem for test MTL loading.
// ---------------------------------------------------------------------------

namespace
{

class TestFileSystem : public IFileSystem
{
public:
    void put(std::string path, std::vector<uint8_t> data)
    {
        files_[std::move(path)] = std::move(data);
    }

    [[nodiscard]] std::vector<uint8_t> read(std::string_view path) override
    {
        auto it = files_.find(std::string(path));
        if (it == files_.end())
            return {};
        return it->second;
    }

    [[nodiscard]] bool exists(std::string_view path) override
    {
        return files_.count(std::string(path)) > 0;
    }

    [[nodiscard]] std::string resolve(std::string_view base, std::string_view relative) override
    {
        // Simple: strip filename from base, append relative.
        std::string baseStr(base);
        auto slash = baseStr.find_last_of('/');
        if (slash != std::string::npos)
            return baseStr.substr(0, slash + 1) + std::string(relative);
        return std::string(relative);
    }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};

// A simple triangle OBJ string for the most basic test.
const std::string kTriangleObj = R"(
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 0.0 1.0 0.0

vn 0.0 0.0 1.0
vn 0.0 0.0 1.0
vn 0.0 0.0 1.0

vt 0.0 0.0
vt 1.0 0.0
vt 0.0 1.0

f 1/1/1 2/2/2 3/3/3
)";

// A cube OBJ with 6 quad faces (triangulated by the loader → 12 triangles).
const std::string kCubeObj = R"(
v -0.5 -0.5  0.5
v  0.5 -0.5  0.5
v  0.5  0.5  0.5
v -0.5  0.5  0.5
v -0.5 -0.5 -0.5
v  0.5 -0.5 -0.5
v  0.5  0.5 -0.5
v -0.5  0.5 -0.5

vn  0.0  0.0  1.0
vn  0.0  0.0 -1.0
vn  1.0  0.0  0.0
vn -1.0  0.0  0.0
vn  0.0  1.0  0.0
vn  0.0 -1.0  0.0

vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0

f 1/1/1 2/2/1 3/3/1 4/4/1
f 6/1/2 5/2/2 8/3/2 7/4/2
f 2/1/3 6/2/3 7/3/3 3/4/3
f 5/1/4 1/2/4 4/3/4 8/4/4
f 4/1/5 3/2/5 7/3/5 8/4/5
f 5/1/6 6/2/6 2/3/6 1/4/6
)";

std::vector<uint8_t> toBytes(const std::string& s)
{
    return {s.begin(), s.end()};
}

}  // namespace

TEST_CASE("ObjLoader reports .obj extension", "[assets]")
{
    ObjLoader loader;
    auto exts = loader.extensions();
    REQUIRE(exts.size() == 1);
    REQUIRE(exts[0] == ".obj");
}

TEST_CASE("ObjLoader decodes a simple triangle", "[assets]")
{
    ObjLoader loader;
    TestFileSystem fs;

    auto bytes = toBytes(kTriangleObj);
    auto result = loader.decode(bytes, "test.obj", fs);

    auto* scene = std::get_if<CpuSceneData>(&result);
    REQUIRE(scene != nullptr);

    REQUIRE(scene->meshes.size() == 1);
    const auto& mesh = scene->meshes[0];

    // 3 vertices, 3 indices (one triangle).
    REQUIRE(mesh.positions.size() == 9);  // 3 verts × 3 floats
    REQUIRE(mesh.indices.size() == 3);

    // Should have normals (oct-encoded: 2 values per vertex).
    REQUIRE(mesh.normals.size() == 6);  // 3 verts × 2 snorm16

    // Should have UVs (float16 encoded: 2 values per vertex).
    REQUIRE(mesh.uvs.size() == 6);  // 3 verts × 2 uint16

    // Should have generated tangents (4 values per vertex).
    REQUIRE(mesh.tangents.size() == 12);  // 3 verts × 4 uint8

    // Verify positions.
    CHECK(mesh.positions[0] == 0.0f);
    CHECK(mesh.positions[1] == 0.0f);
    CHECK(mesh.positions[2] == 0.0f);
    CHECK(mesh.positions[3] == 1.0f);
    CHECK(mesh.positions[4] == 0.0f);
    CHECK(mesh.positions[5] == 0.0f);
    CHECK(mesh.positions[6] == 0.0f);
    CHECK(mesh.positions[7] == 1.0f);
    CHECK(mesh.positions[8] == 0.0f);
}

TEST_CASE("ObjLoader decodes a cube with quads", "[assets]")
{
    ObjLoader loader;
    TestFileSystem fs;

    auto bytes = toBytes(kCubeObj);
    auto result = loader.decode(bytes, "cube.obj", fs);

    auto* scene = std::get_if<CpuSceneData>(&result);
    REQUIRE(scene != nullptr);

    REQUIRE(scene->meshes.size() == 1);
    const auto& mesh = scene->meshes[0];

    // 6 faces × 4 verts = 24 unique vertices (each face has unique normal).
    // Each quad is triangulated into 2 triangles → 12 triangles → 36 indices.
    REQUIRE(mesh.indices.size() == 36);

    // Unique vertex count: each face has 4 unique combos of pos/norm/uv = 24.
    const size_t vertCount = mesh.positions.size() / 3;
    REQUIRE(vertCount == 24);

    // Surface attributes should be present.
    REQUIRE(mesh.normals.size() == vertCount * 2);
    REQUIRE(mesh.uvs.size() == vertCount * 2);
    REQUIRE(mesh.tangents.size() == vertCount * 4);

    // Bounding box should be [-0.5, 0.5] on all axes.
    CHECK(mesh.boundsMin.x == -0.5f);
    CHECK(mesh.boundsMin.y == -0.5f);
    CHECK(mesh.boundsMin.z == -0.5f);
    CHECK(mesh.boundsMax.x == 0.5f);
    CHECK(mesh.boundsMax.y == 0.5f);
    CHECK(mesh.boundsMax.z == 0.5f);

    // Scene nodes should have one root node.
    REQUIRE(scene->nodes.size() == 1);
    REQUIRE(scene->rootNodeIndices.size() == 1);
}

TEST_CASE("ObjLoader handles OBJ without normals or UVs", "[assets]")
{
    ObjLoader loader;
    TestFileSystem fs;

    const std::string bareObj = R"(
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 0.0 1.0 0.0
f 1 2 3
)";

    auto bytes = toBytes(bareObj);
    auto result = loader.decode(bytes, "bare.obj", fs);

    auto* scene = std::get_if<CpuSceneData>(&result);
    REQUIRE(scene != nullptr);
    REQUIRE(scene->meshes.size() == 1);

    const auto& mesh = scene->meshes[0];
    REQUIRE(mesh.positions.size() == 9);
    REQUIRE(mesh.indices.size() == 3);

    // No normals or UVs in the OBJ → surface attributes should be empty.
    CHECK(mesh.normals.empty());
    CHECK(mesh.uvs.empty());
    CHECK(mesh.tangents.empty());
}

TEST_CASE("ObjLoader parses MTL materials", "[assets]")
{
    ObjLoader loader;
    TestFileSystem fs;

    const std::string mtl = R"(
newmtl red_material
Kd 1.0 0.0 0.0
Ns 200.0
)";

    const std::string obj = R"(
mtllib test.mtl
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 0.0 1.0 0.0
vn 0.0 0.0 1.0
vt 0.0 0.0
usemtl red_material
f 1/1/1 2/1/1 3/1/1
)";

    fs.put("dir/test.mtl", toBytes(mtl));

    auto bytes = toBytes(obj);
    auto result = loader.decode(bytes, "dir/model.obj", fs);

    auto* scene = std::get_if<CpuSceneData>(&result);
    REQUIRE(scene != nullptr);

    // Should have parsed one material.
    REQUIRE(scene->materials.size() == 1);
    CHECK(scene->materials[0].albedo.x == 1.0f);
    CHECK(scene->materials[0].albedo.y == 0.0f);
    CHECK(scene->materials[0].albedo.z == 0.0f);

    // Roughness derived from Ns=200: 1 - sqrt(200/1000) ≈ 0.553.
    CHECK(scene->materials[0].roughness < 0.6f);
    CHECK(scene->materials[0].roughness > 0.5f);

    // Node should reference the material.
    REQUIRE(scene->nodes.size() == 1);
    CHECK(scene->nodes[0].materialIndex == 0);
}
