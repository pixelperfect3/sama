kmkk## JSON Integration Architecture for Nimbus Engine

### Document: `docs/JSON_ARCHITECTURE.md`

---

### 1. Build Integration

rapidjson is header-only (~300KB). It follows the same integration pattern as `stb`: `FetchContent_Declare` + `FetchContent_Populate` (no CMake targets needed), then expose the include path to consuming targets.

**Addition to `/Users/shayanj/claude/engine/CMakeLists.txt`**, placed after the `soloud` block (around line 98):

```cmake
# rapidjson — header-only JSON parser (MIT). FetchContent_Populate like stb;
# no CMake targets needed for a header-only lib.
FetchContent_Declare(
    rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG        v1.1.0
)
FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
endif()
```

A new static library `engine_io` is added:

```cmake
add_library(engine_io STATIC engine/io/Json.cpp)
target_include_directories(engine_io PUBLIC ${CMAKE_SOURCE_DIR} ${rapidjson_SOURCE_DIR}/include)
target_link_libraries(engine_io PUBLIC engine glm::glm)
```

Only `engine_io` links against the rapidjson include path. All other targets that need JSON link `engine_io`, never rapidjson directly. This enforces the wrapper boundary at the build level.

`engine_scene` gains a dependency on `engine_io` for the serializer:

```cmake
target_link_libraries(engine_scene PUBLIC engine_ecs engine_io glm::glm)
```

---

### 2. `engine::io` Wrapper API

**File layout:**
- `/Users/shayanj/claude/engine/engine/io/Json.h` -- public header (the only file other engine modules include)
- `/Users/shayanj/claude/engine/engine/io/Json.cpp` -- implementation (includes rapidjson headers)

The wrapper provides three types: `JsonDocument`, `JsonValue`, and `JsonWriter`. rapidjson headers are never included in `Json.h`; the implementation uses the pimpl idiom to keep rapidjson types out of the public interface.

#### 2.1 JsonValue (read-only view)

```cpp
namespace engine::io
{

// Opaque handle to a value within a parsed JSON document.
// Lightweight (pointer-sized). Only valid while the owning JsonDocument is alive.
class JsonValue
{
public:
    // Type queries
    bool isNull() const;
    bool isBool() const;
    bool isInt() const;
    bool isUint() const;
    bool isFloat() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    // Typed accessors — assert on type mismatch in debug, UB in release.
    bool        getBool() const;
    int32_t     getInt() const;
    uint32_t    getUint() const;
    float       getFloat() const;
    const char* getString() const;

    // Typed accessors with fallback — return defaultVal on type mismatch.
    bool        getBool(bool defaultVal) const;
    int32_t     getInt(int32_t defaultVal) const;
    uint32_t    getUint(uint32_t defaultVal) const;
    float       getFloat(float defaultVal) const;
    const char* getString(const char* defaultVal) const;

    // Math helpers — read directly into engine math types.
    // Expects JSON arrays of the correct length: [x,y], [x,y,z], [x,y,z,w].
    math::Vec2 getVec2() const;
    math::Vec3 getVec3() const;
    math::Vec4 getVec4() const;
    math::Quat getQuat() const;  // [x, y, z, w]

    // Object member access — returns a null JsonValue if key not found.
    JsonValue operator[](const char* key) const;
    bool hasMember(const char* key) const;

    // Array access
    std::size_t arraySize() const;
    JsonValue   operator[](std::size_t index) const;

    // Iteration (range-for support)
    class Iterator;
    Iterator begin() const;  // iterates array elements or object members
    Iterator end() const;

    // For object iteration: name of current member (valid only during object iteration)
    const char* memberName() const;

private:
    friend class JsonDocument;
    const void* val_ = nullptr;  // points to rapidjson::Value; opaque to callers
};

}  // namespace engine::io
```

The `Iterator` class wraps `rapidjson::Value::ConstValueIterator` (for arrays) or `rapidjson::Value::ConstMemberIterator` (for objects), yielding `JsonValue` on dereference. For objects, each yielded `JsonValue` exposes `memberName()` for the key and the value accessors for the value.

#### 2.2 JsonDocument (owns parsed data)

```cpp
namespace engine::io
{

class JsonDocument
{
public:
    JsonDocument();
    ~JsonDocument();

    // Move-only (owns the rapidjson allocator)
    JsonDocument(JsonDocument&&) noexcept;
    JsonDocument& operator=(JsonDocument&&) noexcept;
    JsonDocument(const JsonDocument&) = delete;
    JsonDocument& operator=(const JsonDocument&) = delete;

    // Parse from in-memory string. Returns false on parse error.
    // After failure, errorMessage() and errorOffset() describe the problem.
    bool parse(const char* json, std::size_t length);
    bool parse(std::string_view json);

    // Parse from file on disk. Returns false if file cannot be read or parse fails.
    bool parseFile(const char* path);

    // Root value of the parsed document.
    JsonValue root() const;

    // Error reporting after a failed parse.
    const char* errorMessage() const;
    std::size_t errorOffset() const;
    bool        hasError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::io
```

The `Impl` struct holds a `rapidjson::Document` (which uses `rapidjson::MemoryPoolAllocator` by default). The pimpl ensures no rapidjson types leak into the header.

#### 2.3 JsonWriter (builds JSON output)

```cpp
namespace engine::io
{

class JsonWriter
{
public:
    // If prettyPrint is true, output is indented (dev/editor use).
    // If false, compact output (shipped config files, baked assets).
    explicit JsonWriter(bool prettyPrint = true);
    ~JsonWriter();

    JsonWriter(JsonWriter&&) noexcept;
    JsonWriter& operator=(JsonWriter&&) noexcept;
    JsonWriter(const JsonWriter&) = delete;
    JsonWriter& operator=(const JsonWriter&) = delete;

    // Structure
    void startObject();
    void endObject();
    void startArray();
    void endArray();
    void key(const char* name);

    // Scalars
    void writeBool(bool v);
    void writeInt(int32_t v);
    void writeUint(uint32_t v);
    void writeFloat(float v);
    void writeString(const char* v);
    void writeNull();

    // Math helpers — write as JSON arrays
    void writeVec2(const math::Vec2& v);
    void writeVec3(const math::Vec3& v);
    void writeVec4(const math::Vec4& v);
    void writeQuat(const math::Quat& v);  // [x, y, z, w]

    // Retrieve the built JSON string.
    const char* getString() const;
    std::size_t getLength() const;

    // Write directly to a file. Returns false on I/O error.
    bool writeToFile(const char* path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace engine::io
```

The `Impl` holds either a `rapidjson::Writer<StringBuffer>` or `rapidjson::PrettyWriter<StringBuffer>` depending on the `prettyPrint` flag. Both are SAX-style writers; no intermediate DOM is built.

---

### 3. Scene Serialization Format

The JSON schema for a scene file. This format is used during development; the build pipeline bakes it to a binary format for shipped builds.

```json
{
  "version": 1,
  "entities": [
    {
      "id": 0,
      "name": "Sun",
      "components": {
        "Transform": {
          "position": [0.0, 10.0, 0.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        },
        "DirectionalLight": {
          "direction": [-0.3, -1.0, -0.5],
          "color": [1.0, 0.95, 0.9],
          "intensity": 3.0,
          "castShadows": true
        }
      }
    },
    {
      "id": 1,
      "name": "MainCamera",
      "components": {
        "Transform": {
          "position": [0.0, 2.0, 5.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        },
        "Camera": {
          "fovY": 60.0,
          "nearPlane": 0.1,
          "farPlane": 1000.0,
          "type": "Perspective",
          "viewLayer": 0
        }
      }
    },
    {
      "id": 2,
      "name": "Rock",
      "parent": null,
      "components": {
        "Transform": {
          "position": [3.0, 0.0, -2.0],
          "rotation": [0.0, 0.707, 0.0, 0.707],
          "scale": [1.0, 1.0, 1.0]
        },
        "Mesh": {
          "asset": "meshes/rock.glb"
        },
        "Material": {
          "albedo": [0.6, 0.55, 0.5, 1.0],
          "roughness": 0.9,
          "metallic": 0.0,
          "emissiveScale": 0.0,
          "albedoMap": "textures/rock_albedo.png",
          "normalMap": "textures/rock_normal.png",
          "ormMap": "textures/rock_orm.png",
          "emissiveMap": "",
          "occlusionMap": ""
        }
      }
    },
    {
      "id": 3,
      "name": "Rock_Detail",
      "parent": 2,
      "components": {
        "Transform": {
          "position": [0.5, 0.0, 0.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [0.5, 0.5, 0.5]
        },
        "Mesh": {
          "asset": "meshes/pebble.glb"
        }
      }
    }
  ]
}
```

**Design decisions in the format:**

- **`id` field**: A scene-local integer, not the runtime `EntityID`. Runtime entity IDs are generated fresh on load. The scene-local ID exists solely to express parent-child relationships within the file.
- **`parent` field**: References the scene-local `id` of the parent entity. `null` or absent means root entity. This is simpler and more git-diff-friendly than a nested hierarchy.
- **`components` object**: Each key is a component type name. The serializer maintains a registry of known component names mapped to serialize/deserialize functions.
- **Asset references**: Stored as relative paths (strings), not runtime handles. On load, the scene serializer calls `AssetManager::load()` for each referenced asset.
- **Material**: Scalar PBR properties are stored inline. Texture maps are stored as asset paths (empty string = no texture).
- **Rotation**: Stored as quaternion `[x, y, z, w]` to avoid gimbal lock and match `math::Quat` (which is `glm::quat`).
- **`version` field**: Allows schema migration in future. The serializer checks this on load and can apply transforms for older versions.

**Supported component types (initial set):**

| JSON key | Engine type | Namespace |
|---|---|---|
| `Transform` | `TransformComponent` | `engine::rendering` |
| `Camera` | `CameraComponent` | `engine::rendering` |
| `Mesh` | `MeshComponent` | `engine::rendering` |
| `Material` | `MaterialComponent` + `Material` data | `engine::rendering` |
| `DirectionalLight` | `DirectionalLightComponent` | `engine::rendering` |
| `PointLight` | `PointLightComponent` | `engine::rendering` |
| `SpotLight` | `SpotLightComponent` | `engine::rendering` |

Additional component types are added by registering serialization functions; the serializer does not need modification for each new component.

---

### 4. SceneSerializer Class

**File layout:**
- `/Users/shayanj/claude/engine/engine/scene/SceneSerializer.h`
- `/Users/shayanj/claude/engine/engine/scene/SceneSerializer.cpp`

```cpp
namespace engine::scene
{

class SceneSerializer
{
public:
    // Callback types for extensible component serialization.
    // SerializeFn: given an entity and writer, serialize one component to JSON.
    // DeserializeFn: given an entity, JSON value, and resources, attach a component.
    using SerializeFn   = std::function<void(ecs::EntityID, const ecs::Registry&,
                                             const rendering::RenderResources&,
                                             io::JsonWriter&)>;
    using DeserializeFn = std::function<void(ecs::EntityID, ecs::Registry&,
                                             rendering::RenderResources&,
                                             assets::AssetManager&,
                                             io::JsonValue)>;

    // Register a named component type with its serialize/deserialize functions.
    void registerComponent(const char* name, SerializeFn serialize, DeserializeFn deserialize);

    // Register all built-in engine component types (Transform, Camera, Mesh,
    // Material, lights). Called once at engine startup.
    void registerEngineComponents();

    // Save the entire scene to a JSON file.
    // Iterates all entities in the registry, serializes each registered component.
    bool saveScene(const ecs::Registry& reg,
                   const rendering::RenderResources& resources,
                   const char* filepath);

    // Load a scene from a JSON file.
    // Creates entities, attaches components, rebuilds hierarchy.
    // Asset references are queued for async loading via AssetManager.
    bool loadScene(const char* filepath,
                   ecs::Registry& reg,
                   rendering::RenderResources& resources,
                   assets::AssetManager& assetManager);

private:
    struct ComponentHandler
    {
        std::string name;
        SerializeFn serialize;
        DeserializeFn deserialize;
    };

    std::vector<ComponentHandler> handlers_;
};

}  // namespace engine::scene
```

**Save algorithm (`saveScene`):**

1. Create a `JsonWriter` with pretty-print enabled.
2. Write `{ "version": 1, "entities": [ ... ] }`.
3. Iterate all live entities in the registry. For each entity:
   a. Assign a scene-local ID (sequential counter).
   b. Store the mapping `EntityID -> scene-local ID` in a temporary map.
   c. Write the entity's `id` and `name` (from a `NameComponent` if present, otherwise omit).
   d. Look up the entity's parent via `scene::getParent()`. If non-null, write `"parent": <scene-local-id-of-parent>`.
   e. For each registered component handler, check if the entity has that component. If so, call the serialize function.
4. Write to file via `JsonWriter::writeToFile()`.

**Load algorithm (`loadScene`):**

1. Parse the file via `JsonDocument::parseFile()`.
2. Check `version`.
3. First pass: iterate the `entities` array. For each entry:
   a. Create an entity via `reg.createEntity()`.
   b. Store the mapping `scene-local-id -> EntityID`.
   c. For each key in the `components` object, look up the registered handler by name and call the deserialize function.
4. Second pass: iterate entities again to rebuild hierarchy. For each entry with a `parent` field:
   a. Resolve the parent's scene-local ID to its runtime `EntityID`.
   b. Call `scene::setParent(reg, child, parent)`.

The two-pass approach is necessary because a child entity may appear in the file before its parent.

**Mesh and Material loading specifics:**

- `Mesh` deserialization: The asset path is passed to `AssetManager::load<CpuSceneData>(path)`. The resulting handle is stored. The `MeshComponent::mesh` ID is set once the asset finishes loading (requires either polling in a system or a callback mechanism). For the initial implementation, scene loading is synchronous: assets are loaded before the scene is considered ready.
- `Material` deserialization: Scalar PBR values are written directly to a `Material` struct and registered in `RenderResources` via `addMaterial()`. Texture paths are loaded via `AssetManager`; texture IDs are assigned once uploads complete.

---

### 5. Config Files

#### 5.1 RenderSettings

**File:** `config/render_settings.json`

```json
{
  "shadows": {
    "enabled": true,
    "directionalRes": 2048,
    "spotRes": 1024,
    "cascadeCount": 3,
    "maxDistance": 150.0,
    "filter": "PCF4x4"
  },
  "lighting": {
    "maxActiveLights": 256,
    "iblEnabled": true
  },
  "postProcess": {
    "ssao": {
      "enabled": true,
      "radius": 0.5,
      "bias": 0.025,
      "sampleCount": 16
    },
    "bloom": {
      "enabled": true,
      "threshold": 1.0,
      "intensity": 0.04,
      "downsampleSteps": 5
    },
    "fxaaEnabled": true,
    "toneMapper": "ACES",
    "exposure": 1.0
  },
  "depthPrepassEnabled": true,
  "depthPrepassAlphaTestedOnly": false,
  "anisotropicFiltering": 8,
  "renderScale": 1.0
}
```

**Implementation:** Two free functions in a new file `engine/rendering/RenderSettingsJson.h` / `.cpp`:

```cpp
namespace engine::rendering
{
    // Returns a RenderSettings populated from JSON. Missing fields use the
    // platform default (from renderSettingsPlatformDefault).
    RenderSettings loadRenderSettings(const char* filepath, const GpuFeatures& gpu);

    // Write current settings to JSON (pretty-printed, for user config persistence).
    bool saveRenderSettings(const RenderSettings& settings, const char* filepath);
}
```

These functions live in `engine/rendering/` rather than `engine/io/` because they are domain-specific. They depend on `engine_io` for the JSON wrapper.

#### 5.2 InputBindings

**File:** `config/input_bindings.json`

```json
{
  "keys": {
    "W": "MoveForward",
    "S": "MoveBack",
    "Space": "Jump",
    "Escape": "Pause"
  },
  "mouseButtons": {
    "Left": "Attack",
    "Right": "Block"
  },
  "axes": [
    { "name": "MoveX", "negative": "A", "positive": "D" },
    { "name": "MoveY", "negative": "S", "positive": "W" }
  ]
}
```

**Implementation:** Two free functions in `engine/input/ActionMapJson.h` / `.cpp`:

```cpp
namespace engine::input
{
    // Populate an ActionMap from a JSON bindings file.
    bool loadActionMap(const char* filepath, ActionMap& map);

    // Persist current bindings to JSON (for player rebinding).
    bool saveActionMap(const ActionMap& map, const char* filepath);
}
```

Key names use the `Key` enum string representation (a string-to-enum table is needed; this is a small lookup table mapping e.g. `"W"` to `Key::W`).

---

### 6. File Layout Summary

```
engine/
  io/
    Json.h              -- public API: JsonDocument, JsonValue, JsonWriter
    Json.cpp            -- implementation (includes rapidjson internally)
  scene/
    SceneSerializer.h   -- SceneSerializer class
    SceneSerializer.cpp -- save/load logic, built-in component handlers
    HierarchyComponents.h  (existing)
    SceneGraph.h           (existing)
    SceneGraph.cpp         (existing)
    TransformSystem.h      (existing)
    TransformSystem.cpp    (existing)
  rendering/
    RenderSettingsJson.h   -- loadRenderSettings / saveRenderSettings
    RenderSettingsJson.cpp
  input/
    ActionMapJson.h        -- loadActionMap / saveActionMap
    ActionMapJson.cpp
tests/
  io/
    TestJson.cpp           -- unit tests for Json wrapper
  scene/
    TestSceneSerializer.cpp -- round-trip scene tests
  rendering/
    TestRenderSettingsJson.cpp
  input/
    TestActionMapJson.cpp
```

**CMake additions:**

```cmake
# engine_io
add_library(engine_io STATIC engine/io/Json.cpp)
target_include_directories(engine_io PUBLIC ${CMAKE_SOURCE_DIR} ${rapidjson_SOURCE_DIR}/include)
target_link_libraries(engine_io PUBLIC engine glm::glm)
```

Update existing targets:
- `engine_scene`: add `engine_io` to `target_link_libraries`
- `engine_rendering`: add `engine/rendering/RenderSettingsJson.cpp` to sources, add `engine_io` to link
- `engine_input` (or the relevant input library): add `engine/input/ActionMapJson.cpp` to sources, add `engine_io` to link
- `engine_tests`: add new test files, add `engine_io` to link

---

### 7. Test Plan

All tests use Catch2, consistent with the existing test suite at `/Users/shayanj/claude/engine/tests/`.

#### 7.1 `tests/io/TestJson.cpp` -- JSON Wrapper Unit Tests

| Test case | Description |
|---|---|
| Parse valid JSON object | Parse `{"key": "value"}`, verify `root().isObject()`, `root()["key"].getString()` |
| Parse valid JSON array | Parse `[1, 2, 3]`, verify `arraySize() == 3`, element access |
| Parse failure returns error | Parse `{invalid`, verify `hasError()`, `errorMessage()` non-empty |
| Typed getters: bool, int, uint, float, string | Parse each type, verify correct accessor returns expected value |
| Default-value getters on type mismatch | `getInt(42)` on a string value returns 42 |
| Null and missing member | `root()["nonexistent"].isNull()` returns true |
| Vec3 round-trip | Write `[1.0, 2.0, 3.0]` with `JsonWriter::writeVec3`, parse back, verify `getVec3()` |
| Quat round-trip | Same for quaternion |
| Object iteration | Parse `{"a":1, "b":2}`, iterate, verify keys and values |
| Array iteration | Parse `[10, 20, 30]`, iterate, verify values |
| Writer produces valid JSON | Build object with `JsonWriter`, parse the output with `JsonDocument`, verify values |
| Writer file I/O | Write to temp file, read back, verify content |
| Parse from file | Write known JSON to disk, `parseFile()`, verify values |
| Empty document | Parse `{}`, verify `root().isObject()`, no members |
| Nested objects | Parse `{"a": {"b": 1}}`, verify `root()["a"]["b"].getInt() == 1` |
| Large array (performance sanity) | Parse a 10,000-element array, verify `arraySize()` |

#### 7.2 `tests/scene/TestSceneSerializer.cpp` -- Scene Serialization Tests

| Test case | Description |
|---|---|
| Round-trip empty scene | Save empty registry, load back, verify zero entities |
| Round-trip single entity with Transform | Create entity with `TransformComponent`, save, load into fresh registry, verify position/rotation/scale match |
| Round-trip hierarchy | Create parent-child relationship, save, load, verify `getParent()` and `getChildren()` are correct |
| Round-trip deep hierarchy (3 levels) | Grandparent-parent-child, verify all relationships |
| Round-trip camera entity | `CameraComponent` fields survive save/load |
| Round-trip directional light | `DirectionalLightComponent` fields survive |
| Round-trip point light | `PointLightComponent` fields survive |
| Round-trip spot light | `SpotLightComponent` fields survive |
| Material scalar properties | `Material` albedo, roughness, metallic survive round-trip (no textures) |
| Material with texture paths | Texture asset paths are preserved in JSON (no actual asset loading in unit tests) |
| Multiple entities | Save 5 entities with various components, load, verify all present |
| Entity ordering independence | Verify that a child appearing before its parent in the JSON still loads correctly |
| Version field present | Saved file contains `"version": 1` |
| Unknown component ignored on load | Add `"FutureComponent": {}` to JSON, load does not fail, other components still load |
| Missing component fields use defaults | `Transform` with only `position` specified, `rotation` and `scale` get defaults |

#### 7.3 `tests/rendering/TestRenderSettingsJson.cpp`

| Test case | Description |
|---|---|
| Round-trip default settings | `renderSettingsHigh()` -> save -> load -> compare all fields |
| Partial file uses defaults | JSON with only `shadows.enabled: false`, all other fields get platform default values |
| Enum string mapping | `"PCF8x8"` parses to `ShadowFilter::PCF8x8`, `"ACES"` parses to `ToneMapper::ACES` |
| Invalid enum string falls back to default | `"filter": "NonExistent"` -> uses default filter |
| Empty file uses full defaults | Empty `{}` -> returns `renderSettingsPlatformDefault` |

#### 7.4 `tests/input/TestActionMapJson.cpp`

| Test case | Description |
|---|---|
| Round-trip key bindings | Bind W->MoveForward, save, load into fresh ActionMap, verify `keyAction(Key::W)` |
| Round-trip mouse bindings | Bind Left->Attack, save, load, verify |
| Round-trip axis bindings | Bind MoveX (A/D), save, load, verify `axisBinding("MoveX")` |
| Unknown key name ignored | JSON with `"FakeKey": "Action"` does not crash, other bindings load |
| Empty file produces empty map | No bindings, no crash |

---

### Implementation Sequence

The work is ordered so each step produces a testable, compilable unit:

1. **CMake + rapidjson fetch** -- Add the `FetchContent_Declare`/`FetchContent_Populate` block and the `engine_io` library target. Verify the build still works.
2. **`engine/io/Json.h` and `Json.cpp`** -- Implement `JsonDocument`, `JsonValue`, `JsonWriter` with full pimpl. Write `tests/io/TestJson.cpp` in parallel. This is the foundation everything else depends on.
3. **`engine/scene/SceneSerializer.h/.cpp`** -- Implement the serializer with `registerEngineComponents()` covering `TransformComponent`, `CameraComponent`, `DirectionalLightComponent`, `PointLightComponent`, `SpotLightComponent`. Leave `Mesh` and `Material` serialization for step 4. Write `tests/scene/TestSceneSerializer.cpp`.
4. **Mesh and Material serialization** -- Add `Mesh` and `Material` component handlers to `SceneSerializer::registerEngineComponents()`. This requires asset path tracking (a `NameComponent` or similar mechanism to remember which asset path produced a given `MeshComponent::mesh` ID). Extend the serializer tests.
5. **`RenderSettingsJson`** -- Implement load/save for `RenderSettings`. Write tests.
6. **`ActionMapJson`** -- Implement load/save for `ActionMap`. Requires a string-to-`Key` lookup table. Write tests.

### Potential Challenges

- **Asset path tracking**: `MeshComponent` stores a `uint32_t` ID into `RenderResources`, not the original file path. For save to work, the engine needs a reverse mapping from resource ID to asset path. Options: (a) add an `AssetPathComponent` that stores the original path alongside the mesh/material, or (b) query `AssetManager` for the path by handle. Option (a) is simpler and does not couple the serializer to `AssetManager` internals.
- **Entity enumeration**: The `Registry` currently does not expose an "iterate all live entities" API (it has `View<T>` for component queries). The serializer either needs a new `Registry::forEachEntity(callback)` method, or it queries for entities that have at least a `TransformComponent` (which every scene entity should have). Adding `forEachEntity` is cleaner and is a small addition to `Registry`.
- **String lifetimes in ActionMap**: `ActionMap` stores `std::string_view` for action names, requiring static/global-lifetime strings. The JSON loader reads strings from a parsed document that will be destroyed. The loader must copy action name strings into stable storage (e.g., a `std::vector<std::string>` owned by a config manager, or change `ActionMap` to own `std::string` instead of `std::string_view`).
- **rapidjson v1.1.0 compiler warnings**: rapidjson 1.1.0 (2016) emits warnings under modern C++20 compilers. Mitigate with `target_compile_options(engine_io PRIVATE -Wno-class-memaccess)` or use the `master` branch which has fixes. The `master` branch is acceptable since rapidjson is header-only and the wrapper isolates it.

---

### Critical Files for Implementation
- `/Users/shayanj/claude/engine/engine/io/Json.h` -- the core wrapper API that everything else depends on
- `/Users/shayanj/claude/engine/engine/io/Json.cpp` -- implementation containing all rapidjson usage
- `/Users/shayanj/claude/engine/engine/scene/SceneSerializer.h` -- the scene save/load interface
- `/Users/shayanj/claude/engine/engine/scene/SceneSerializer.cpp` -- serialization logic and built-in component handlers
- `/Users/shayanj/claude/engine/CMakeLists.txt` -- build integration (FetchContent for rapidjson, engine_io library target, dependency wiring)