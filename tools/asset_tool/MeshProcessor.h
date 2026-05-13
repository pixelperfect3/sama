#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "tools/asset_tool/AssetProcessor.h"

namespace engine::tools
{

// ---------------------------------------------------------------------------
// SamaMesh (.smsh) binary format — written by MeshProcessor, designed to be
// loadable + inspectable.  Self-contained; no external schema needed.
//
// All values little-endian.  Vertex stream is interleaved float32×3 positions.
// Surface attributes (normals/tangents/UVs) are NOT carried in this LOD chain
// container — that's a future extension.  The runtime today still consumes
// .glb/.obj via cgltf/ObjLoader; .smsh is for the LOD-aware path.
//
// Layout:
//   char[4]  magic        = "SMSH"
//   uint32   version      = 1
//   uint32   vertexCount  (count of position triples)
//   uint32   lodCount     (1..4)
//   uint32   indexType    (2 = uint16, 4 = uint32 — only 2 used today)
//   for each LOD i in [0..lodCount):
//       uint32 indexCount_i
//       uint32 indexOffset_i  (bytes from end-of-header to first index of LOD i)
//   float32[vertexCount * 3]  positions (LOD 0's vertex order, reused by all LODs)
//   uint16[sum(indexCount_i)] indices   (concatenated LOD index buffers)
// ---------------------------------------------------------------------------

inline constexpr uint32_t kSamaMeshMagic = 0x48534D53u;  // 'S','M','S','H' little-endian
inline constexpr uint32_t kSamaMeshVersion = 1u;

struct SamaMeshLodSpec
{
    uint32_t indexCount = 0;
    uint32_t indexOffset = 0;  // bytes into index blob
};

struct SamaMeshHeader
{
    uint32_t magic = kSamaMeshMagic;
    uint32_t version = kSamaMeshVersion;
    uint32_t vertexCount = 0;
    uint32_t lodCount = 0;
    uint32_t indexType = 2;  // bytes-per-index
};

// ---------------------------------------------------------------------------
// MeshProcessor — discovers .obj / .glb / .gltf inputs and writes optimized
// .smsh containers with a per-tier LOD chain.
//
// Pipeline per mesh:
//   1. Decode source into positions + uint16 indices (a single submesh; the
//      first mesh in the file is taken — minimal scope, see Phase D notes).
//   2. Run meshopt_optimizeVertexCache + Overdraw + VertexFetch on LOD 0
//      (lossless re-ordering).
//   3. For each tier-specified LOD target, run meshopt_simplify and re-run
//      vertex-cache optimization on the simplified index buffer.
//   4. Write .smsh side-by-side with the source.
//
// Tier LOD targets (fraction of source index count):
//   low  = [0.25]
//   mid  = [0.50, 0.25]
//   high = [0.50, 0.25, 0.10]
//
// Empty / degenerate inputs are skipped (no crash).
// ---------------------------------------------------------------------------

class MeshProcessor
{
public:
    MeshProcessor(const CliArgs& args, const TierConfig& tier);

    /// Per-tier LOD reduction targets (fractions of source index count).
    /// Index 0 is always the highest-detail LOD past the source (e.g. 0.5).
    static std::vector<float> lodTargetsForTier(const std::string& tierName);

    /// Process a single in-memory mesh into a packed .smsh blob.
    /// Returns true on success and writes to outBytes.
    /// Both positions and indices are consumed by value-copy; the function
    /// runs optimization + simplification internally.
    bool processInMemory(const std::vector<float>& positions, const std::vector<uint32_t>& indices,
                         std::vector<uint8_t>& outBytes);

    /// Discover .obj inputs (glTF / glb still need full cgltf integration; for
    /// now MeshProcessor handles wavefront .obj — same surface the existing
    /// pipeline copies untouched).
    std::vector<AssetEntry> discover();

    /// Process all .obj mesh entries: decode, optimize, and write .smsh.
    /// Entries are mutated in-place to update output extension + format.
    void processAll(std::vector<AssetEntry>& entries);

    /// Compute vertex-cache hit rate for an index buffer (meshopt analyzer).
    /// Public for tests.
    static float analyzeCacheACMR(const std::vector<uint32_t>& indices, uint32_t vertexCount);

private:
    bool processOne(AssetEntry& entry);
    bool readObj(const std::filesystem::path& path, std::vector<float>& positions,
                 std::vector<uint32_t>& indices);

    CliArgs args_;
    TierConfig tier_;
};

}  // namespace engine::tools
