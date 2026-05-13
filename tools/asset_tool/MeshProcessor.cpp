#include "tools/asset_tool/MeshProcessor.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace engine::tools
{

// ---------------------------------------------------------------------------
// Tier LOD targets
// ---------------------------------------------------------------------------

std::vector<float> MeshProcessor::lodTargetsForTier(const std::string& tierName)
{
    if (tierName == "low")
    {
        return {0.25f};
    }
    if (tierName == "high")
    {
        return {0.50f, 0.25f, 0.10f};
    }
    // mid (default)
    return {0.50f, 0.25f};
}

// ---------------------------------------------------------------------------
// Vertex-cache ACMR (average cache-miss ratio).  Lower is better; 1.0 means
// every triangle vertex was a cache miss.  Use as an upper bound — values
// below ~1.0 indicate the cache optimizer made forward progress.
// ---------------------------------------------------------------------------

float MeshProcessor::analyzeCacheACMR(const std::vector<uint32_t>& indices, uint32_t vertexCount)
{
    if (indices.empty() || vertexCount == 0)
    {
        return 0.0f;
    }
    // 16-entry FIFO cache approximation — meshopt's default Tipsify-style model.
    auto stats =
        meshopt_analyzeVertexCache(indices.data(), indices.size(), vertexCount, 16u, 0u, 0u);
    return stats.acmr;
}

// ---------------------------------------------------------------------------
// processInMemory — core LOD + optimization pipeline.
// ---------------------------------------------------------------------------

bool MeshProcessor::processInMemory(const std::vector<float>& positionsIn,
                                    const std::vector<uint32_t>& indicesIn,
                                    std::vector<uint8_t>& outBytes)
{
    // Degenerate guards — don't crash on empty / one-triangle inputs.
    if (positionsIn.empty() || positionsIn.size() % 3 != 0)
    {
        return false;
    }
    if (indicesIn.empty() || indicesIn.size() % 3 != 0)
    {
        return false;
    }

    const uint32_t vertexCount = static_cast<uint32_t>(positionsIn.size() / 3);
    if (vertexCount == 0)
    {
        return false;
    }

    // Bounds check: index values must be < vertexCount.
    for (uint32_t idx : indicesIn)
    {
        if (idx >= vertexCount)
        {
            return false;
        }
    }

    std::vector<float> positions = positionsIn;
    std::vector<uint32_t> baseIndices = indicesIn;

    // ---------------------------------------------------------------------
    // Lossless re-ordering on LOD 0: cache → overdraw → vertex fetch.
    // ---------------------------------------------------------------------

    // 1) Vertex cache optimization.
    meshopt_optimizeVertexCache(baseIndices.data(), baseIndices.data(), baseIndices.size(),
                                vertexCount);

    // 2) Overdraw optimization (reorders triangles to reduce overdraw).
    //    Threshold 1.05 = allow up to 5% cache-loss in exchange for overdraw wins.
    meshopt_optimizeOverdraw(baseIndices.data(), baseIndices.data(), baseIndices.size(),
                             positions.data(), vertexCount, sizeof(float) * 3, 1.05f);

    // 3) Vertex fetch optimization — reorder vertices so they're accessed
    //    sequentially.  Mutates positions in place and re-maps indices.
    meshopt_optimizeVertexFetch(positions.data(), baseIndices.data(), baseIndices.size(),
                                positions.data(), vertexCount, sizeof(float) * 3);

    // ---------------------------------------------------------------------
    // Build LOD chain.  Each LOD's indices are simplified from LOD 0's
    // optimized index buffer, then re-cache-optimized.
    // ---------------------------------------------------------------------

    std::vector<std::vector<uint32_t>> lodIndices;
    lodIndices.push_back(baseIndices);

    const auto targets = lodTargetsForTier(tier_.name);
    for (float frac : targets)
    {
        const size_t targetIdxCount =
            std::max<size_t>(3, static_cast<size_t>(baseIndices.size() * frac) / 3 * 3);

        std::vector<uint32_t> lod(baseIndices.size());
        float lodError = 0.0f;
        const size_t newIdxCount =
            meshopt_simplify(lod.data(), baseIndices.data(), baseIndices.size(), positions.data(),
                             vertexCount, sizeof(float) * 3, targetIdxCount,
                             1e-1f /* target_error */, 0u /* options */, &lodError);
        lod.resize(newIdxCount);

        if (newIdxCount >= 3)
        {
            meshopt_optimizeVertexCache(lod.data(), lod.data(), lod.size(), vertexCount);
            lodIndices.push_back(std::move(lod));
        }
        else
        {
            // Simplifier collapsed to nothing — duplicate the previous LOD to
            // keep the chain monotone-or-better.
            lodIndices.push_back(lodIndices.back());
        }
    }

    // ---------------------------------------------------------------------
    // Serialize: header + per-LOD specs + positions + concatenated indices.
    // ---------------------------------------------------------------------

    // Compute byte budget.
    SamaMeshHeader header;
    header.vertexCount = vertexCount;
    header.lodCount = static_cast<uint32_t>(lodIndices.size());
    header.indexType = 2;  // uint16

    // Output must fit in uint16 — clamp vertexCount conservatively.  Larger
    // meshes should split into multiple .smsh blobs; for now we reject them
    // rather than silently switching to uint32.
    if (vertexCount > 0xFFFFu)
    {
        return false;
    }

    const size_t lodSpecBytes = lodIndices.size() * sizeof(SamaMeshLodSpec);
    const size_t posBytes = static_cast<size_t>(vertexCount) * 3 * sizeof(float);
    size_t totalIdxBytes = 0;
    for (const auto& lod : lodIndices)
    {
        totalIdxBytes += lod.size() * sizeof(uint16_t);
    }

    const size_t totalBytes = sizeof(SamaMeshHeader) + lodSpecBytes + posBytes + totalIdxBytes;
    outBytes.assign(totalBytes, 0);

    size_t cursor = 0;
    std::memcpy(outBytes.data() + cursor, &header, sizeof(header));
    cursor += sizeof(header);

    // Per-LOD specs (offsets are byte offsets into the index blob).
    {
        uint32_t off = 0;
        for (size_t i = 0; i < lodIndices.size(); ++i)
        {
            SamaMeshLodSpec spec;
            spec.indexCount = static_cast<uint32_t>(lodIndices[i].size());
            spec.indexOffset = off;
            std::memcpy(outBytes.data() + cursor, &spec, sizeof(spec));
            cursor += sizeof(spec);
            off += static_cast<uint32_t>(lodIndices[i].size() * sizeof(uint16_t));
        }
    }

    // Positions.
    std::memcpy(outBytes.data() + cursor, positions.data(), posBytes);
    cursor += posBytes;

    // Indices (downcast to uint16).
    for (const auto& lod : lodIndices)
    {
        for (uint32_t v : lod)
        {
            uint16_t v16 = static_cast<uint16_t>(v);
            std::memcpy(outBytes.data() + cursor, &v16, sizeof(uint16_t));
            cursor += sizeof(uint16_t);
        }
    }

    return cursor == totalBytes;
}

// ---------------------------------------------------------------------------
// .obj reader — minimal, positions-only (drops UVs, normals, materials).
// Index lines support v / v/vt / v/vt/vn — we keep only the position index.
// ---------------------------------------------------------------------------

MeshProcessor::MeshProcessor(const CliArgs& args, const TierConfig& tier) : args_(args), tier_(tier)
{
}

bool MeshProcessor::readObj(const fs::path& path, std::vector<float>& positions,
                            std::vector<uint32_t>& indices)
{
    std::ifstream in(path);
    if (!in)
    {
        return false;
    }

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "v")
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            iss >> x >> y >> z;
            positions.push_back(x);
            positions.push_back(y);
            positions.push_back(z);
        }
        else if (tag == "f")
        {
            // Parse 3+ vertices; triangulate fan-style (v0, vi, vi+1).
            std::vector<uint32_t> face;
            std::string token;
            while (iss >> token)
            {
                // Split on '/' and take the first component.
                std::string posTok = token.substr(0, token.find('/'));
                if (posTok.empty())
                {
                    continue;
                }
                int v = 0;
                try
                {
                    v = std::stoi(posTok);
                }
                catch (...)
                {
                    return false;
                }
                // .obj is 1-based; negative values index from end.
                uint32_t idx = 0;
                if (v > 0)
                {
                    idx = static_cast<uint32_t>(v - 1);
                }
                else if (v < 0)
                {
                    idx = static_cast<uint32_t>(static_cast<int64_t>(positions.size() / 3) + v);
                }
                face.push_back(idx);
            }
            if (face.size() < 3)
            {
                continue;
            }
            for (size_t i = 1; i + 1 < face.size(); ++i)
            {
                indices.push_back(face[0]);
                indices.push_back(face[i]);
                indices.push_back(face[i + 1]);
            }
        }
    }

    return !positions.empty() && !indices.empty();
}

// ---------------------------------------------------------------------------
// Discovery — surface .obj files for LOD processing.
// (.glb/.gltf still go through the as-is copy path in AssetProcessor.)
// ---------------------------------------------------------------------------

std::vector<AssetEntry> MeshProcessor::discover()
{
    std::vector<AssetEntry> result;

    std::error_code ec;
    if (!fs::exists(args_.inputDir))
    {
        return result;
    }

    for (auto& p : fs::recursive_directory_iterator(args_.inputDir, ec))
    {
        if (ec)
            break;
        if (!p.is_regular_file())
            continue;

        std::string ext = p.path().extension().string();
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (ext != ".obj")
            continue;

        std::string relPath = fs::relative(p.path(), args_.inputDir).string();

        AssetEntry entry;
        entry.type = "mesh";
        entry.source = relPath;
        // Output: same path, replace .obj with .smsh
        fs::path outPath = relPath;
        outPath.replace_extension(".smsh");
        entry.output = outPath.string();
        entry.format = "smsh";

        if (args_.verbose)
        {
            std::cout << "  Found mesh: " << relPath << "\n";
        }
        result.push_back(std::move(entry));
    }

    return result;
}

void MeshProcessor::processAll(std::vector<AssetEntry>& entries)
{
    for (auto& entry : entries)
    {
        if (entry.type != "mesh")
        {
            continue;
        }
        if (!processOne(entry))
        {
            std::cerr << "Warning: mesh processing failed for " << entry.source << "\n";
        }
    }
}

bool MeshProcessor::processOne(AssetEntry& entry)
{
    fs::path srcPath = fs::path(args_.inputDir) / entry.source;
    fs::path dstPath = fs::path(args_.outputDir) / entry.output;

    std::vector<float> positions;
    std::vector<uint32_t> indices;
    if (!readObj(srcPath, positions, indices))
    {
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!processInMemory(positions, indices, bytes))
    {
        return false;
    }

    fs::create_directories(dstPath.parent_path());

    std::ofstream out(dstPath, std::ios::binary);
    if (!out)
    {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out)
    {
        return false;
    }

    if (args_.verbose)
    {
        std::cout << "  Wrote mesh: " << entry.output << " (" << bytes.size() << " bytes, "
                  << positions.size() / 3 << " verts, " << indices.size() / 3 << " src tris)\n";
    }

    return true;
}

}  // namespace engine::tools
