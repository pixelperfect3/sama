#include "engine/assets/AssetManager.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "engine/assets/GltfAsset.h"
#include "engine/assets/IAssetLoader.h"
#include "engine/assets/IFileSystem.h"
#include "engine/assets/Texture.h"
#include "engine/rendering/MeshBuilder.h"
#include "engine/threading/ThreadPool.h"

namespace engine::assets
{

const std::string AssetManager::kEmptyError;

// ---------------------------------------------------------------------------

AssetManager::AssetManager(threading::ThreadPool& pool, IFileSystem& fs) : pool_(pool), fs_(fs)
{
    // records_[0] is the invalid sentinel — never allocated, never freed.
    records_.emplace_back();
}

AssetManager::~AssetManager()
{
    // Drain any pending uploads so workers don't reference freed memory.
    pool_.waitAll();
    processUploads();
}

// ---------------------------------------------------------------------------
// Loader registration
// ---------------------------------------------------------------------------

void AssetManager::registerLoader(std::unique_ptr<IAssetLoader> loader)
{
    loaders_.push_back(std::move(loader));
}

// ---------------------------------------------------------------------------
// Slot management
// ---------------------------------------------------------------------------

uint32_t AssetManager::allocSlot(std::string_view path)
{
    uint32_t idx;
    if (!freeList_.empty())
    {
        idx = freeList_.back();
        freeList_.pop_back();
        Record& rec = records_[idx];
        const uint32_t prevGen = rec.generation;
        rec = Record{};
        rec.generation = prevGen;  // already bumped by processUploads() when the slot was freed
    }
    else
    {
        idx = static_cast<uint32_t>(records_.size());
        records_.emplace_back();
    }

    records_[idx].path = std::string(path);
    records_[idx].refCount = 1;
    pathToSlot_[std::string(path)] = idx;
    return idx;
}

void AssetManager::scheduleDestroy(uint32_t index)
{
    // Remove from path map so the path can be reloaded if desired.
    pathToSlot_.erase(records_[index].path);
    // Defer actual slot reclamation by one frame so bgfx can flush.
    pendingFree_.push_back(index);
}

bool AssetManager::isHandleValid(uint32_t index, uint32_t generation) const
{
    if (index == 0 || index >= records_.size())
        return false;
    return records_[index].generation == generation;
}

IAssetLoader* AssetManager::findLoader(std::string_view path) const
{
    // Match on file extension (case-insensitive).
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Strip "#..." sub-asset suffix (e.g. "soldier.gltf#mesh/body" → ".gltf").
    auto hash = ext.find('#');
    if (hash != std::string::npos)
        ext = ext.substr(0, hash);

    for (const auto& loader : loaders_)
    {
        for (auto supported : loader->extensions())
        {
            if (ext == supported)
                return loader.get();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Worker → main thread communication
// ---------------------------------------------------------------------------

void AssetManager::pushUpload(uint32_t index, uint32_t generation, CpuAssetData data)
{
    std::lock_guard lock(uploadMutex_);
    uploadQueue_.push_back({index, generation, std::move(data)});
}

void AssetManager::pushError(uint32_t index, uint32_t generation, std::string msg)
{
    std::lock_guard lock(uploadMutex_);
    uploadQueue_.push_back({index, generation, std::move(msg)});
}

// ---------------------------------------------------------------------------
// processUploads — main thread, once per frame
// ---------------------------------------------------------------------------

void AssetManager::processUploads()
{
    // Free slots from last frame's releases.
    // Bump generation here so any existing handles immediately become stale —
    // even if the slot is not yet reused. allocSlot() then preserves this value.
    for (uint32_t idx : pendingFree_)
    {
        records_[idx].generation++;
        records_[idx].payload.reset();
        records_[idx].state = AssetState::Pending;
        freeList_.push_back(idx);
    }
    pendingFree_.clear();

    // Drain the upload queue.
    std::vector<UploadRequest> pending;
    {
        std::lock_guard lock(uploadMutex_);
        pending = std::move(uploadQueue_);
    }

    for (auto& req : pending)
        uploadOne(req);
}

void AssetManager::uploadOne(UploadRequest& req)
{
    if (!isHandleValid(req.index, req.generation))
        return;  // stale — slot was released and reused before upload arrived

    Record& rec = records_[req.index];

    if (std::holds_alternative<std::string>(req.result))
    {
        rec.state = AssetState::Failed;
        rec.error = std::get<std::string>(req.result);
        return;
    }

    // Dispatch to per-type upload logic via visitor.
    // Each overload creates bgfx handles and stores the result in rec.payload.
    std::visit(
        [&](auto&& data)
        {
            using DataT = std::decay_t<decltype(data)>;
            upload(rec, std::forward<DataT>(data));
        },
        std::get<CpuAssetData>(req.result));
}

// ---------------------------------------------------------------------------
// Mipmap generation helper
// ---------------------------------------------------------------------------

// Creates a bgfx texture from base-level RGBA8
// pixels.  All mip levels are packed into a single immutable memory block and
// passed to createTexture2D in one call — the correct bgfx pattern for
// providing pre-computed mips (updateTexture2D on Metal does not reliably
// initialise levels that were created without initial data).
static bgfx::TextureHandle createTexture2DWithMips(const uint8_t* pixels, int w, int h,
                                                    uint64_t flags)
{
    // Upload mip 0 only.  bgfx's Metal backend computes LOD=max when hasMips=true due to
    // an apparent UV-derivative issue on this GPU, producing near-black output.  Until the
    // root cause is resolved, create textures without a mip chain.  The Metal sampler clamps
    // LOD to 0 automatically when only one level exists, giving correct (if aliased) sampling.
    const bgfx::Memory* mem = bgfx::copy(pixels, static_cast<uint32_t>(w * h * 4));
    return bgfx::createTexture2D(static_cast<uint16_t>(w), static_cast<uint16_t>(h),
                                 /*hasMips=*/false, /*numLayers=*/1,
                                 bgfx::TextureFormat::RGBA8, flags, mem);
}

// ---------------------------------------------------------------------------
// Per-type upload implementations
// ---------------------------------------------------------------------------

void AssetManager::upload(Record& rec, CpuTextureData&& data)
{
    if (data.pixels.empty() || data.width == 0 || data.height == 0)
    {
        rec.state = AssetState::Failed;
        rec.error = "TextureLoader produced empty pixel data";
        return;
    }

    bgfx::TextureHandle handle =
        createTexture2DWithMips(data.pixels.data(), static_cast<int>(data.width),
                                static_cast<int>(data.height), BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE);

    if (!bgfx::isValid(handle))
    {
        rec.state = AssetState::Failed;
        rec.error = "bgfx::createTexture2D failed for: " + rec.path;
        return;
    }

    Texture tex;
    tex.handle = handle;
    tex.width = data.width;
    tex.height = data.height;

    rec.payload = std::move(tex);
    rec.state = AssetState::Ready;
}

void AssetManager::upload(Record& rec, CpuSceneData&& data)
{
    GltfAsset asset;
    asset.rootNodeIndices = std::move(data.rootNodeIndices);

    // Upload textures.
    asset.textures.reserve(data.textures.size());
    for (auto& cpuTex : data.textures)
    {
        Texture tex;
        if (!cpuTex.pixels.empty() && cpuTex.width > 0 && cpuTex.height > 0)
        {
            tex.handle =
                createTexture2DWithMips(cpuTex.pixels.data(), static_cast<int>(cpuTex.width),
                                        static_cast<int>(cpuTex.height),
                                        BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE);
            tex.width = cpuTex.width;
            tex.height = cpuTex.height;
        }
        asset.textures.push_back(std::move(tex));
    }

    // Upload meshes.
    asset.meshes.reserve(data.meshes.size());
    for (auto& meshData : data.meshes)
        asset.meshes.push_back(rendering::buildMesh(meshData));

    // Convert materials.
    // Texture binding (albedoMapId etc.) will be wired in the RenderResources
    // integration phase; for now the scalar PBR parameters are applied.
    asset.materials.reserve(data.materials.size());
    for (const auto& cpuMat : data.materials)
    {
        rendering::Material mat;
        mat.albedo = cpuMat.albedo;
        mat.roughness = cpuMat.roughness;
        mat.metallic = cpuMat.metallic;
        mat.emissiveScale = cpuMat.emissiveScale;
        // Store 1-based indices into asset.textures[] so GltfSceneSpawner can
        // remap them to RenderResources texture IDs at spawn time.
        if (cpuMat.albedoTexIndex >= 0)
            mat.albedoMapId = static_cast<uint32_t>(cpuMat.albedoTexIndex + 1);
        if (cpuMat.normalTexIndex >= 0)
            mat.normalMapId = static_cast<uint32_t>(cpuMat.normalTexIndex + 1);
        if (cpuMat.ormTexIndex >= 0)
            mat.ormMapId = static_cast<uint32_t>(cpuMat.ormTexIndex + 1);
        if (cpuMat.emissiveTexIndex >= 0)
            mat.emissiveMapId = static_cast<uint32_t>(cpuMat.emissiveTexIndex + 1);
        if (cpuMat.occlusionTexIndex >= 0)
            mat.occlusionMapId = static_cast<uint32_t>(cpuMat.occlusionTexIndex + 1);
        asset.materials.push_back(mat);
    }

    // Convert node tree. CpuSceneData::Node and GltfAsset::Node have the same
    // fields but are distinct types; copy field by field.
    asset.nodes.reserve(data.nodes.size());
    for (auto& src : data.nodes)
    {
        GltfAsset::Node dst;
        dst.name = std::move(src.name);
        dst.localTransform = src.localTransform;
        dst.meshIndex = src.meshIndex;
        dst.materialIndex = src.materialIndex;
        dst.childIndices = std::move(src.childIndices);
        asset.nodes.push_back(std::move(dst));
    }

    rec.payload = std::move(asset);
    rec.state = AssetState::Ready;
}

}  // namespace engine::assets
