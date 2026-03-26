# Asset Architecture

Assets are the bridge between files on disk and live GPU resources. The system is designed around one constraint: **file I/O and CPU decode happen off the main thread; GPU upload happens on the main thread**. bgfx does not allow GPU resource creation from worker threads, so the two phases must be separated and synchronized through a queue.

---

## Design Principles

- **Handle-based.** All asset references are `AssetHandle<T>` — a typed `(index, generation)` pair. Handles are cheap to copy, safe to hold across frames, and detect use-after-free via the generation counter.
- **Load-then-query, not load-and-block.** `AssetManager::load<T>()` returns a handle immediately. The caller polls `AssetManager::state(handle)` each frame; once `Ready`, the asset is used. No blocking waits on the main thread.
- **Two-phase pipeline.** Worker threads do file I/O + CPU decode and push a `UploadRequest` to a lock-free queue. The main thread drains the queue once per frame, creates bgfx handles, and marks the asset `Ready`.
- **Compound assets.** A `.gltf` file produces one `GltfAsset` which contains sub-handles for each mesh, texture, and material it defines. Game code references the sub-handles, not the glTF file directly.
- **Platform file abstraction.** All file access goes through `IFileSystem`. Desktop uses `std::filesystem`; iOS reads from the app bundle; Android uses `AAssetManager`. Game code never sees platform paths.
- **Ref-counted lifetime.** Each asset record tracks a reference count. Releasing the last reference marks the slot for eviction. GPU handles are destroyed on the next frame so bgfx can flush in-flight commands first.
- **Editor hot-reload.** A `FileWatcher` monitors source paths and triggers reloads on change. Stripped from shipping builds via `#ifdef ENGINE_EDITOR`.

---

## System Overview

```
Game code
  │  load<Mesh>("soldier.gltf#mesh/body")   → AssetHandle<Mesh>
  │  state(handle)                          → AssetState::Ready
  │  get<Mesh>(handle)                      → const Mesh&
  ▼
┌─────────────────────────────────────────────────────────────────┐
│                         AssetManager                            │
│                                                                 │
│  ┌──────────────────┐   ┌─────────────────────────────────┐    │
│  │  AssetRegistry   │   │       LoaderRegistry             │    │
│  │                  │   │                                  │    │
│  │  handle → record │   │  ".gltf/.glb" → GltfLoader      │    │
│  │  state machine   │   │  ".png/.jpg"  → TextureLoader   │    │
│  │  ref counts      │   │  ".ktx2"      → KtxLoader       │    │
│  └──────────────────┘   └─────────────────────────────────┘    │
│                                    │                            │
│  ┌─────────────────────────────────▼──────────────────────┐    │
│  │               Worker ThreadPool                         │    │
│  │   IFileSystem::read() → IAssetLoader::decode()         │    │
│  │   produces CpuAssetData (CPU-side decoded result)       │    │
│  └────────────────────────────────────────────────────────┘    │
│                                    │                            │
│  ┌─────────────────────────────────▼──────────────────────┐    │
│  │               UploadQueue  (lock-free MPSC)             │    │
│  │   worker pushes CpuAssetData + target handle            │    │
│  └────────────────────────────────────────────────────────┘    │
│                                    │                            │
│  ┌─────────────────────────────────▼──────────────────────┐    │
│  │   processUploads()  — called once per frame, main thread│    │
│  │   bgfx::createVertexBuffer / createTexture2D / ...     │    │
│  │   RenderResources::addMesh / addMaterial / addTexture   │    │
│  │   marks handle Ready                                    │    │
│  └────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

---

## Core Types

### AssetHandle\<T\>

```cpp
template<typename T>
struct AssetHandle
{
    uint32_t index;       // slot in AssetRegistry dense array
    uint32_t generation;  // bumped each time the slot is reused

    [[nodiscard]] bool isValid() const { return index != 0; }

    bool operator==(const AssetHandle&) const = default;
};

// Sentinel
template<typename T>
inline constexpr AssetHandle<T> kInvalidHandle{0, 0};
```

The type parameter `T` is a phantom type that makes `AssetHandle<Mesh>` and `AssetHandle<Texture>` incompatible at compile time. Both compile to the same 8-byte pair.

### AssetState

```cpp
enum class AssetState : uint8_t
{
    Pending,   // queued, worker not yet started
    Loading,   // worker thread is decoding
    Uploading, // CPU decode done, waiting for main-thread GPU upload
    Ready,     // GPU handles live, asset usable
    Failed,    // load or decode error; error string in the record
};
```

### AssetRecord (internal)

```cpp
struct AssetRecord
{
    std::string          path;        // canonical resolved path
    AssetState           state;
    uint32_t             refCount;
    uint32_t             generation;  // current generation for this slot
    std::string          error;       // non-empty on Failed
    // Typed payload stored via type-erased unique_ptr after upload
};
```

---

## Asset Lifecycle

```
load<T>(path)
  │
  ├─ path already tracked? → bump refCount, return existing handle
  │
  ├─ allocate slot in AssetRegistry  (state = Pending)
  ├─ return handle to caller immediately
  │
  └─ submit task to ThreadPool:
       IFileSystem::read(path) → raw bytes
       IAssetLoader::decode(bytes) → CpuAssetData
       UploadQueue::push({handle, cpuData})
       AssetRegistry::setState(handle, Uploading)

─────────────────────── main thread, each frame ────────────────────────

processUploads()
  │
  └─ drain UploadQueue:
       for each {handle, cpuData}:
         IAssetUploader::upload(cpuData) → live GPU handles
         RenderResources::add*(...)
         AssetRegistry::setReady(handle, payload)

─────────────────────── caller each frame ──────────────────────────────

state(handle) == Ready  →  get<T>(handle)  →  const T&

─────────────────────── when done ──────────────────────────────────────

release(handle)
  │
  ├─ decrement refCount
  └─ refCount == 0:
       schedule GPU handle destruction for next frame
       bump generation (invalidates all copies of old handle)
       return slot to free list
```

---

## File System Abstraction

All file access goes through a single interface. No loader or game code ever constructs a platform path directly.

```cpp
class IFileSystem
{
public:
    virtual ~IFileSystem() = default;

    // Synchronous read — called from worker threads.
    // Returns empty vector on failure.
    virtual std::vector<uint8_t> read(std::string_view path) = 0;

    // Check existence without reading (used by hot-reload watcher).
    virtual bool exists(std::string_view path) = 0;

    // Resolve a path relative to a base (for glTF texture references).
    virtual std::string resolve(std::string_view base,
                                std::string_view relative) = 0;
};
```

| Platform | Implementation | Notes |
|---|---|---|
| Desktop (macOS/Windows) | `StdFileSystem` | `std::filesystem::path`, reads from CWD or asset root |
| iOS | `BundleFileSystem` | `[NSBundle mainBundle] pathForResource:ofType:` |
| Android | `AAssetFileSystem` | `AAssetManager_open`, passed in from `android_main` |

---

## Loader Interface

Each file type has a dedicated loader. The loader does no file I/O — it receives raw bytes and returns a CPU-side decoded result.

```cpp
// CPU-side decoded data, ready for GPU upload.
// Variant over all asset types.
using CpuAssetData = std::variant<
    CpuMeshData,
    CpuTextureData,
    CpuMaterialData,
    CpuSceneData
>;

class IAssetLoader
{
public:
    virtual ~IAssetLoader() = default;

    // Returns the file extensions this loader handles (e.g. {".gltf", ".glb"}).
    virtual std::span<const std::string_view> extensions() const = 0;

    // Decode raw bytes into CPU-side asset data.
    // Called from a worker thread — must be thread-safe.
    // path is provided for resolving relative references (e.g. glTF textures).
    virtual CpuAssetData decode(std::span<const uint8_t> bytes,
                                std::string_view path,
                                IFileSystem& fs) = 0;
};
```

### Planned loaders

| Loader | Extensions | Library | Notes |
|---|---|---|---|
| `GltfLoader` | `.gltf`, `.glb` | cgltf (single-header) | Meshes, materials, textures, scene nodes |
| `TextureLoader` | `.png`, `.jpg`, `.bmp` | stb_image (already in project) | Decoded to RGBA8 |
| `KtxLoader` | `.ktx2` | KTX-Software (future) | GPU-compressed ASTC/BC7, shipped builds only |
| `HdrLoader` | `.hdr` | stb_image_float | For IBL environment maps |

---

## glTF Loading — Compound Asset

A single `.gltf` file may contain multiple meshes, materials, and textures. The loader produces a `GltfAsset` — a self-contained result with sub-handles for each item.

```
soldier.gltf
  ├── mesh[0]  "body"      → AssetHandle<Mesh>
  ├── mesh[1]  "helmet"    → AssetHandle<Mesh>
  ├── texture[0] "albedo"  → AssetHandle<Texture>
  ├── texture[1] "normal"  → AssetHandle<Texture>
  ├── material[0]          → AssetHandle<Material>  (refs texture[0], texture[1])
  └── scene node tree      → retained inside GltfAsset for scene spawning
```

Sub-handles are registered in the `AssetRegistry` with synthetic paths (`"soldier.gltf#mesh/body"`, `"soldier.gltf#texture/0"`). This means:
- Two glTF files that share a texture via the same synthetic path will share the `AssetHandle`.
- Game code can also load individual sub-assets directly if the path is known.

### Scene spawning

```cpp
// Spawns the default scene from a loaded GltfAsset into the ECS registry.
// Creates entities with WorldTransformComponent, MeshComponent,
// MaterialComponent, VisibleTag, ShadowVisibleTag matching the glTF node tree.
void GltfSceneSpawner::spawn(const GltfAsset& asset,
                             ecs::Registry& reg,
                             RenderResources& res);
```

This replaces the manual entity setup in `scene_demo/main.mm`.

---

## Upload Queue

A lock-free multiple-producer / single-consumer queue. Worker threads push completed `CpuAssetData`; the main thread drains it in `processUploads()`. Using a lock-free queue avoids a mutex in the hot path — upload is called once per frame, but workers can push at any time.

The queue holds `UploadRequest`:

```cpp
struct UploadRequest
{
    uint32_t     slotIndex;    // target AssetRegistry slot
    uint32_t     generation;   // validate slot hasn't been recycled
    CpuAssetData data;         // CPU-side decoded payload
};
```

`processUploads()` discards requests whose generation doesn't match the slot — this handles the race where a handle is released and the slot reused before the upload arrives.

---

## Memory Management

### Per-frame budget

`processUploads()` accepts an optional `maxUploadBytes` budget. If a frame's upload queue would exceed it, remaining requests are deferred to the next frame. This prevents GPU stalls from large batch uploads (e.g. loading a new level).

### Eviction

When `release()` drops a handle's ref count to 0:
1. GPU destruction is scheduled for the **next** frame (one frame of grace period so bgfx can flush in-flight commands).
2. The slot's generation is bumped — all existing copies of the handle are now stale.
3. The slot is returned to the free list.

No LRU or automatic eviction is planned yet. Levels manage their own asset lifetimes explicitly via `load`/`release` pairs.

---

## Hot-Reload (Editor Only)

`FileWatcher` monitors source paths using OS APIs (`FSEvents` on macOS, `ReadDirectoryChangesW` on Windows, `inotify` on Linux). On a change notification:

1. The watcher calls `AssetManager::invalidate(path)`.
2. The existing handle is kept valid; the asset state transitions back to `Pending`.
3. The load pipeline re-runs from scratch.
4. Once the new upload completes, the handle's payload is atomically swapped.
5. All users of the handle see the new data on the next frame without any code changes.

Handles held by ECS components (`MeshComponent`, `MaterialComponent`) require no update — the handle itself is unchanged; only the data behind it changes.

Stripped from shipping builds with `#ifdef ENGINE_EDITOR`.

---

## Integration with Existing Code

### RenderResources

`RenderResources` currently stores meshes and materials with plain `uint32_t` IDs. The asset system will wrap these: `AssetHandle<Mesh>` encodes the same index with a generation guard. `RenderResources` gains typed `add`/`get`/`remove` overloads that accept handles.

### ECS Components

`MeshComponent` and `MaterialComponent` will migrate from `uint32_t mesh/material` to `AssetHandle<Mesh>` and `AssetHandle<Material>`. The phantom type prevents swapping a mesh handle into a material slot.

### scene_demo

The seven hardcoded `kObjects` and the manual entity loop will be replaced with:
```cpp
auto gltf = assets.load<GltfAsset>("scene.gltf");
// ... wait for Ready state ...
GltfSceneSpawner::spawn(assets.get<GltfAsset>(gltf), reg, res);
```

---

## Phase Plan

| Phase | Work |
|---|---|
| **A** | `AssetHandle<T>`, `AssetRegistry`, `AssetManager` skeleton (no loading yet) |
| **B** | `IFileSystem` + `StdFileSystem` (desktop); `IAssetLoader` interface |
| **C** | `TextureLoader` (stb_image → bgfx texture); `AssetManager::processUploads()` |
| **D** | `GltfLoader` (cgltf → `CpuMeshData` + `CpuMaterialData`); `GltfSceneSpawner` |
| **E** | Migrate `MeshComponent`/`MaterialComponent` to typed handles; update `scene_demo` |
| **F** | `KtxLoader` + GPU-compressed texture path (ASTC mobile, BC7 desktop) |
| **G** | `FileWatcher` + hot-reload (editor builds only) |
| **H** | iOS `BundleFileSystem`; Android `AAssetFileSystem` |

Phases A–E deliver a working glTF pipeline on desktop. F–H are needed before the first mobile build.
