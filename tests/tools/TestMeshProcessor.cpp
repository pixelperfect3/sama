#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "tools/asset_tool/AssetProcessor.h"
#include "tools/asset_tool/MeshProcessor.h"

namespace fs = std::filesystem;
using namespace engine::tools;

namespace
{

// Build a planar triangulated grid (NxN cells -> 2*N*N triangles).  Each
// vertex has unique position so meshopt has room to simplify.
struct Grid
{
    std::vector<float> positions;
    std::vector<uint32_t> indices;
};

Grid makeGrid(int n)
{
    Grid g;
    const int verts = (n + 1) * (n + 1);
    g.positions.reserve(verts * 3);
    for (int y = 0; y <= n; ++y)
    {
        for (int x = 0; x <= n; ++x)
        {
            // Slight random-ish wobble in Z so simplify has features to remove.
            const float z =
                std::sin(static_cast<float>(x) * 0.5f) + std::cos(static_cast<float>(y) * 0.5f);
            g.positions.push_back(static_cast<float>(x));
            g.positions.push_back(static_cast<float>(y));
            g.positions.push_back(z * 0.1f);
        }
    }
    for (int y = 0; y < n; ++y)
    {
        for (int x = 0; x < n; ++x)
        {
            const uint32_t i0 = static_cast<uint32_t>(y * (n + 1) + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + (n + 1);
            const uint32_t i3 = i2 + 1;
            g.indices.push_back(i0);
            g.indices.push_back(i2);
            g.indices.push_back(i1);
            g.indices.push_back(i1);
            g.indices.push_back(i2);
            g.indices.push_back(i3);
        }
    }
    return g;
}

// Decode a .smsh blob's header + per-LOD index counts.
struct SmshInfo
{
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t vertexCount = 0;
    uint32_t lodCount = 0;
    uint32_t indexType = 0;
    std::vector<SamaMeshLodSpec> lods;
};

bool parseSmsh(const std::vector<uint8_t>& bytes, SmshInfo& info)
{
    if (bytes.size() < sizeof(SamaMeshHeader))
        return false;
    SamaMeshHeader hdr;
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    info.magic = hdr.magic;
    info.version = hdr.version;
    info.vertexCount = hdr.vertexCount;
    info.lodCount = hdr.lodCount;
    info.indexType = hdr.indexType;

    size_t cursor = sizeof(SamaMeshHeader);
    const size_t lodSpecBytes = hdr.lodCount * sizeof(SamaMeshLodSpec);
    if (cursor + lodSpecBytes > bytes.size())
        return false;

    info.lods.resize(hdr.lodCount);
    for (uint32_t i = 0; i < hdr.lodCount; ++i)
    {
        std::memcpy(&info.lods[i], bytes.data() + cursor, sizeof(SamaMeshLodSpec));
        cursor += sizeof(SamaMeshLodSpec);
    }
    return true;
}

class TempDir
{
public:
    TempDir()
    {
        path_ = fs::temp_directory_path() /
                ("sama_mesh_test_" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const
    {
        return path_;
    }

private:
    fs::path path_;
};

}  // namespace

// ---------------------------------------------------------------------------
// 1. Tier LOD targets
// ---------------------------------------------------------------------------

TEST_CASE("MeshProcessor::lodTargetsForTier", "[asset_tool][mesh]")
{
    CHECK(MeshProcessor::lodTargetsForTier("low").size() == 1);
    CHECK(MeshProcessor::lodTargetsForTier("mid").size() == 2);
    CHECK(MeshProcessor::lodTargetsForTier("high").size() == 3);

    auto high = MeshProcessor::lodTargetsForTier("high");
    REQUIRE(high.size() == 3);
    CHECK(high[0] == 0.50f);
    CHECK(high[1] == 0.25f);
    CHECK(high[2] == 0.10f);
}

// ---------------------------------------------------------------------------
// 2. LOD chain monotonically decreasing index count
// ---------------------------------------------------------------------------

TEST_CASE("MeshProcessor: LOD index counts decrease at each level", "[asset_tool][mesh]")
{
    CliArgs args;
    args.inputDir = "/tmp/dummy_in";
    args.outputDir = "/tmp/dummy_out";

    Grid g = makeGrid(16);  // 256 cells -> 512 triangles -> 1536 indices

    auto run = [&](const std::string& tier)
    {
        TierConfig tc = getTierConfig(tier);
        MeshProcessor mp(args, tc);
        std::vector<uint8_t> bytes;
        REQUIRE(mp.processInMemory(g.positions, g.indices, bytes));
        SmshInfo info;
        REQUIRE(parseSmsh(bytes, info));
        CHECK(info.magic == kSamaMeshMagic);
        CHECK(info.version == kSamaMeshVersion);
        CHECK(info.vertexCount == g.positions.size() / 3);
        CHECK(info.lodCount == 1u + MeshProcessor::lodTargetsForTier(tier).size());

        // LOD 0 should be the optimized full mesh — same count as source.
        CHECK(info.lods[0].indexCount == g.indices.size());

        for (size_t i = 1; i < info.lods.size(); ++i)
        {
            CHECK(info.lods[i].indexCount <= info.lods[i - 1].indexCount);
        }
        // The last LOD should be substantially smaller than LOD 0.
        CHECK(info.lods.back().indexCount * 2 < info.lods[0].indexCount);
    };

    run("low");
    run("mid");
    run("high");
}

// ---------------------------------------------------------------------------
// 3. Vertex-cache ACMR improves after optimization
// ---------------------------------------------------------------------------

TEST_CASE("MeshProcessor: vertex-cache ACMR improves", "[asset_tool][mesh]")
{
    Grid g = makeGrid(20);
    const uint32_t vertexCount = static_cast<uint32_t>(g.positions.size() / 3);

    // Shuffle the index buffer to drive baseline ACMR high.
    std::vector<uint32_t> shuffled = g.indices;
    // Simple deterministic shuffle: swap every-other triangle with one from the
    // far end of the buffer.  This produces a worst-case access pattern.
    const size_t tris = shuffled.size() / 3;
    for (size_t i = 0; i < tris / 2; i += 2)
    {
        const size_t a = i * 3;
        const size_t b = (tris - 1 - i) * 3;
        for (int k = 0; k < 3; ++k)
            std::swap(shuffled[a + k], shuffled[b + k]);
    }

    const float baselineAcmr = MeshProcessor::analyzeCacheACMR(shuffled, vertexCount);

    CliArgs args;
    TierConfig tc = getTierConfig("mid");
    MeshProcessor mp(args, tc);

    std::vector<uint8_t> bytes;
    REQUIRE(mp.processInMemory(g.positions, shuffled, bytes));

    // Parse LOD 0 indices back out of the blob.
    SmshInfo info;
    REQUIRE(parseSmsh(bytes, info));

    const size_t hdrSize = sizeof(SamaMeshHeader) + info.lodCount * sizeof(SamaMeshLodSpec);
    const size_t posBytes = info.vertexCount * 3 * sizeof(float);
    const size_t idxBlobStart = hdrSize + posBytes;

    std::vector<uint32_t> lod0;
    lod0.reserve(info.lods[0].indexCount);
    for (uint32_t i = 0; i < info.lods[0].indexCount; ++i)
    {
        uint16_t v = 0;
        std::memcpy(&v, bytes.data() + idxBlobStart + i * sizeof(uint16_t), sizeof(uint16_t));
        lod0.push_back(v);
    }

    const float optimizedAcmr = MeshProcessor::analyzeCacheACMR(lod0, info.vertexCount);

    INFO("baseline ACMR = " << baselineAcmr << " optimized ACMR = " << optimizedAcmr);
    CHECK(optimizedAcmr < baselineAcmr);
    // Reasonable upper bound for a well-optimized planar grid.
    CHECK(optimizedAcmr < 1.5f);
}

// ---------------------------------------------------------------------------
// 4. Degenerate inputs don't crash
// ---------------------------------------------------------------------------

TEST_CASE("MeshProcessor: degenerate inputs are rejected, not crashing", "[asset_tool][mesh]")
{
    CliArgs args;
    TierConfig tc = getTierConfig("mid");
    MeshProcessor mp(args, tc);

    std::vector<uint8_t> bytes;

    // Empty everything.
    CHECK_FALSE(mp.processInMemory({}, {}, bytes));

    // Positions only, no indices.
    CHECK_FALSE(mp.processInMemory({0, 0, 0, 1, 0, 0, 0, 1, 0}, {}, bytes));

    // Indices only, no positions.
    CHECK_FALSE(mp.processInMemory({}, {0, 1, 2}, bytes));

    // Positions not divisible by 3.
    CHECK_FALSE(mp.processInMemory({0, 0, 0, 1}, {0, 1, 2}, bytes));

    // Indices not divisible by 3.
    CHECK_FALSE(mp.processInMemory({0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 1}, bytes));

    // Out-of-range index.
    CHECK_FALSE(mp.processInMemory({0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 1, 99}, bytes));

    // Exactly one valid triangle — should succeed.
    CHECK(mp.processInMemory({0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 1, 2}, bytes));
    SmshInfo info;
    REQUIRE(parseSmsh(bytes, info));
    CHECK(info.vertexCount == 3);
    CHECK(info.lods[0].indexCount == 3);
    // LODs after the trivial mesh: simplifier may collapse below 3 indices, in
    // which case MeshProcessor duplicates the previous LOD.  Index count must
    // always be a multiple of 3 (full triangles).
    for (auto& lod : info.lods)
    {
        CHECK(lod.indexCount % 3 == 0);
        CHECK(lod.indexCount >= 3);
    }
}

// ---------------------------------------------------------------------------
// 5. End-to-end: .obj discovery + .smsh write via processAll
// ---------------------------------------------------------------------------

TEST_CASE("MeshProcessor: end-to-end .obj -> .smsh", "[asset_tool][mesh]")
{
    TempDir in;
    TempDir out;

    // Write a tiny .obj — a square (2 triangles).
    {
        std::ofstream o(in.path() / "square.obj");
        o << "v 0 0 0\n"
          << "v 1 0 0\n"
          << "v 1 1 0\n"
          << "v 0 1 0\n"
          << "f 1 2 3\n"
          << "f 1 3 4\n";
    }

    CliArgs args;
    args.inputDir = in.path().string();
    args.outputDir = out.path().string();
    args.target = "android";
    args.tier = "mid";

    TierConfig tc = getTierConfig(args.tier);
    MeshProcessor mp(args, tc);

    auto entries = mp.discover();
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].type == "mesh");
    CHECK(entries[0].source == "square.obj");
    CHECK(entries[0].output == "square.smsh");
    CHECK(entries[0].format == "smsh");

    mp.processAll(entries);

    fs::path produced = out.path() / "square.smsh";
    REQUIRE(fs::exists(produced));

    std::ifstream f(produced, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());

    SmshInfo info;
    REQUIRE(parseSmsh(bytes, info));
    CHECK(info.magic == kSamaMeshMagic);
    CHECK(info.vertexCount == 4);
    CHECK(info.lods[0].indexCount == 6);
}
