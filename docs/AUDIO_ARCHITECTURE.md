# Audio Architecture for Nimbus Engine

## Document: `docs/AUDIO_ARCHITECTURE.md`

---

### 1. Technology Stack

**SoLoud** (MIT, ~500KB-1MB compiled) as the game-facing audio engine providing mixing, 3D spatialization, distance attenuation, Doppler, looping, and fading. **miniaudio** (public domain, single header) as the platform audio backend abstraction, covering CoreAudio (macOS/iOS), AAudio (Android), and WASAPI (Windows).

This combination was chosen over FMOD (3-6MB, royalty-bearing) per the decision recorded in `docs/NOTES.md`. The `IAudioEngine` abstraction layer preserves FMOD as a drop-in upgrade path.

---

### 2. Build Integration

SoLoud should be added via `FetchContent`, following the exact pattern used by Catch2, glm, glfw, and bgfx in the root `CMakeLists.txt`. SoLoud's CMake build supports selecting backends at configure time; miniaudio is selected and all others disabled.

```
FetchContent_Declare(
    soloud
    GIT_REPOSITORY https://github.com/jarikomppa/soloud.git
    GIT_TAG        RELEASE_20200207
)
```

SoLoud does not ship a first-class CMakeLists.txt, so one of two approaches is needed:

**Option A (recommended):** Vendor SoLoud sources into `third_party/soloud/` and write a minimal `CMakeLists.txt` that compiles the core + miniaudio backend into a static library. This mirrors how `stb` is handled (FetchContent_Populate without targets) but adds a custom target. SoLoud is ~30 source files; a hand-written CMakeLists.txt is straightforward and avoids depending on SoLoud's contrib/ build system.

**Option B:** Use FetchContent_Populate (like `stb`) and define the `engine_audio` static library target in the root CMakeLists.txt, listing the SoLoud source files explicitly.

Either way, the result is an `engine_audio` static library target:

```cmake
add_library(engine_audio STATIC
    engine/audio/SoLoudAudioEngine.cpp
    engine/audio/AudioSystem.cpp
    engine/audio/AudioClipLoader.cpp
    # SoLoud core sources (vendored or fetched)
    ${SOLOUD_SRC_DIR}/core/soloud.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_audiosource.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_bus.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_3d.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_basicops.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_faderops.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_filterops.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_getters.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_setters.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_voicegroup.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_core_voiceops.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_fft.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_filter.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_thread.cpp
    ${SOLOUD_SRC_DIR}/audiosource/wav/soloud_wav.cpp
    ${SOLOUD_SRC_DIR}/audiosource/wav/soloud_wavstream.cpp
    ${SOLOUD_SRC_DIR}/audiosource/wav/stb_vorbis.c
    ${SOLOUD_SRC_DIR}/backend/miniaudio/soloud_miniaudio.cpp
)

target_compile_definitions(engine_audio PRIVATE WITH_MINIAUDIO)
target_include_directories(engine_audio PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(engine_audio PRIVATE ${SOLOUD_SRC_DIR}/include)
target_link_libraries(engine_audio PUBLIC engine_ecs engine_scene glm::glm)
```

On Apple, add `-framework AudioToolbox -framework CoreAudio` link flags, mirroring how `engine_rendering` adds Metal frameworks.

---

### 3. ECS Components

All components follow the conventions in `engine/rendering/EcsComponents.h`: fields ordered largest-alignment-first, explicit padding, `static_assert` on `sizeof` and `offsetof`, documented byte layout.

Components live in `engine/audio/AudioComponents.h` under `namespace engine::audio`.

#### 3.1 AudioSourceComponent

Attached to any entity that emits sound. References an audio clip asset and controls playback parameters.

```cpp
struct AudioSourceComponent          // offset  size
{
    uint32_t clipId;                 //  0       4   index into AudioClip table
    uint32_t busHandle;              //  4       4   SoLoud voice handle (0 = not playing)
    float    volume;                 //  8       4   [0.0, 1.0]
    float    minDistance;            // 12       4   distance at which attenuation begins
    float    maxDistance;            // 16       4   distance at which sound is inaudible
    float    pitch;                  // 20       4   playback speed multiplier (1.0 = normal)
    SoundCategory category;          // 24       1   enum: SFX, Music, UI, Ambient
    uint8_t  flags;                  // 25       1   bit 0: loop, bit 1: spatial (3D),
                                     //              bit 2: autoPlay, bit 3: playing
    uint8_t  _pad[2];               // 26       2
};  // total: 28 bytes
static_assert(sizeof(AudioSourceComponent) == 28);
```

The `busHandle` is an opaque SoLoud voice handle stored here so the system can update position and stop individual sounds. A value of 0 means the sound is not currently playing. The `flags` field uses bit flags matching the pattern used by `TransformComponent::flags` (bit 0: dirty) and `DirectionalLightComponent::flags` (bit 0: castShadows).

#### 3.2 AudioListenerComponent

Tag component placed on the entity whose world transform defines the audio listener position and orientation. Typically the camera entity.

```cpp
struct AudioListenerComponent        // offset  size
{
    uint8_t priority;                //  0       1   for multi-listener: higher wins
    uint8_t _pad[3];                //  1       3
};  // total: 4 bytes
static_assert(sizeof(AudioListenerComponent) == 4);
```

Only one listener is active per frame. If multiple entities have `AudioListenerComponent`, the one with the highest `priority` is used. This is a data component rather than an empty tag so that editor tooling can distinguish primary from secondary listeners.

#### 3.3 SoundCategory enum

```cpp
enum class SoundCategory : uint8_t
{
    SFX     = 0,
    Music   = 1,
    UI      = 2,
    Ambient = 3,
    Count   = 4
};
```

#### 3.4 Spatial Audio and WorldTransformComponent

`AudioSystem` reads `WorldTransformComponent` (defined in `engine/rendering/EcsComponents.h`) for both sources and the listener. This component is written by `TransformSystem` each frame and contains the 4x4 world matrix. The audio system extracts position from column 3 and forward/up vectors from columns 2 and 1 respectively.

No new transform component is needed. Audio piggybacks on the existing scene graph, which is the correct design: a sound-emitting entity already has a `TransformComponent` and `WorldTransformComponent` by virtue of being in the scene.

---

### 4. IAudioEngine Interface

Located at `engine/audio/IAudioEngine.h`. This is the abstraction boundary that isolates SoLoud specifics from the rest of the engine, following the same pattern as `IInputBackend` in the input system.

```cpp
namespace engine::audio
{

using SoundHandle = uint32_t;
inline constexpr SoundHandle INVALID_SOUND = 0;

class IAudioEngine
{
public:
    virtual ~IAudioEngine() = default;

    // Lifecycle
    virtual bool init(uint32_t sampleRate = 44100,
                      uint32_t bufferSize = 2048,
                      uint32_t maxVoices = 32) = 0;
    virtual void shutdown() = 0;

    // Listener (one active listener per frame)
    virtual void setListenerPosition(const math::Vec3& pos) = 0;
    virtual void setListenerOrientation(const math::Vec3& forward,
                                         const math::Vec3& up) = 0;

    // Playback
    virtual SoundHandle play(uint32_t clipId, float volume = 1.0f,
                              bool loop = false) = 0;
    virtual SoundHandle play3D(uint32_t clipId, const math::Vec3& pos,
                                float volume = 1.0f, bool loop = false) = 0;
    virtual void stop(SoundHandle handle) = 0;
    virtual void stopAll() = 0;
    virtual bool isPlaying(SoundHandle handle) const = 0;

    // Per-voice control
    virtual void setVolume(SoundHandle handle, float volume) = 0;
    virtual void setPitch(SoundHandle handle, float pitch) = 0;
    virtual void setPosition(SoundHandle handle,
                              const math::Vec3& pos) = 0;
    virtual void setLooping(SoundHandle handle, bool loop) = 0;

    // Category volumes (master mix)
    virtual void setCategoryVolume(SoundCategory cat, float volume) = 0;
    virtual float getCategoryVolume(SoundCategory cat) const = 0;

    // Global
    virtual void setMasterVolume(float volume) = 0;
    virtual float getMasterVolume() const = 0;

    // Audio clip management
    virtual uint32_t loadClip(const uint8_t* data, size_t size,
                               bool streaming = false) = 0;
    virtual void unloadClip(uint32_t clipId) = 0;

    // Frame update (called by AudioSystem after updating positions)
    virtual void update3dAudio() = 0;
};

}  // namespace engine::audio
```

**Design notes:**

- `SoundHandle` wraps SoLoud's `unsigned int` voice handle. The INVALID_SOUND sentinel (0) matches SoLoud's convention where handle 0 is never a valid voice.
- `loadClip` / `unloadClip` manage the internal clip table. Clip IDs are indices into a dense vector of `SoLoud::Wav` or `SoLoud::WavStream` objects.
- `update3dAudio()` calls `SoLoud::Soloud::update3dAudio()` which recomputes 3D panning, attenuation, and Doppler for all active 3D voices. This must happen after all source positions and the listener have been updated.
- Category volumes are implemented via SoLoud buses: one `SoLoud::Bus` per `SoundCategory`, all routed to the master bus. `setCategoryVolume` calls `Bus::setVolume()`.

---

### 5. SoLoudAudioEngine (Concrete Implementation)

Located at `engine/audio/SoLoudAudioEngine.h` and `engine/audio/SoLoudAudioEngine.cpp`.

```cpp
class SoLoudAudioEngine final : public IAudioEngine
{
    // ...
private:
    SoLoud::Soloud soloud_;
    std::array<SoLoud::Bus, static_cast<size_t>(SoundCategory::Count)> buses_;
    std::array<unsigned int, static_cast<size_t>(SoundCategory::Count)> busHandles_;
    std::vector<std::unique_ptr<SoLoud::AudioSource>> clips_;  // indexed by clipId
    std::vector<uint32_t> freeClipSlots_;
};
```

**Initialization:** `init()` calls `soloud_.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::MINIAUDIO, sampleRate, bufferSize, maxVoices)`. Then plays each bus into the main mixer, storing the bus voice handles.

**Shutdown:** `soloud_.deinit()` in `shutdown()`. Called from the destructor as a safety net.

**Play routing:** `play()` and `play3D()` route through the correct category bus: `buses_[cat].play(clip)` for 2D, `buses_[cat].play3d(clip, x, y, z)` for 3D.

---

### 6. AudioSystem (ECS System)

Located at `engine/audio/AudioSystem.h` and `engine/audio/AudioSystem.cpp`.

```cpp
namespace engine::audio
{

class AudioSystem
{
public:
    explicit AudioSystem(IAudioEngine& engine);

    // Must be called AFTER TransformSystem::update() each frame.
    void update(ecs::Registry& reg);

private:
    IAudioEngine& engine_;
};

}  // namespace engine::audio
```

This follows the exact pattern of `TransformSystem::update(ecs::Registry& reg)` -- a method that takes a registry reference and iterates relevant views.

#### 6.1 Execution Order

The frame loop must call systems in this order:

1. `TransformSystem::update(reg)` -- computes `WorldTransformComponent` from `TransformComponent` hierarchy
2. `AudioSystem::update(reg)` -- reads `WorldTransformComponent` for listener and source positions
3. Rendering systems (`FrustumCullSystem`, `DrawCallBuildSystem`, etc.)

AudioSystem runs after TransformSystem because it needs up-to-date world positions. It runs before (or in parallel with) rendering systems because audio and rendering are independent -- they both only read `WorldTransformComponent`.

In the compile-time DAG scheduler (`buildSchedule<Systems...>()`), AudioSystem would declare:

```cpp
using Reads  = TypeList<WorldTransformComponent, AudioSourceComponent, AudioListenerComponent>;
using Writes = TypeList<AudioSourceComponent>;  // updates busHandle field
```

This makes it parallelizable with all rendering systems that only read `WorldTransformComponent`.

#### 6.2 Update Logic

```
AudioSystem::update(reg):
    1. Find the active listener entity:
       - View<AudioListenerComponent, WorldTransformComponent>
       - Select the entity with highest AudioListenerComponent::priority
       - Extract position from WorldTransformComponent::matrix[3]
       - Extract forward from -matrix[2], up from matrix[1]
       - Call engine_.setListenerPosition(pos)
       - Call engine_.setListenerOrientation(forward, up)

    2. Iterate all audio sources:
       - View<AudioSourceComponent, WorldTransformComponent>
       - For each entity:
         a. If autoPlay flag set and busHandle == 0:
            - busHandle = engine_.play3D(clipId, worldPos, volume, loop)
            - Clear autoPlay flag
         b. If busHandle != 0 and engine_.isPlaying(busHandle):
            - engine_.setPosition(busHandle, worldPos)
            - engine_.setVolume(busHandle, volume)
            - engine_.setPitch(busHandle, pitch)
         c. If busHandle != 0 and !engine_.isPlaying(busHandle):
            - busHandle = 0  (voice finished naturally)
            - Clear playing flag

    3. Call engine_.update3dAudio()
```

#### 6.3 Fire-and-Forget vs Managed Sounds

Two playback modes:

- **Managed (component-driven):** Entity has `AudioSourceComponent`. AudioSystem tracks the voice handle, updates position each frame, and detects when playback finishes. Used for looping ambient sounds, music, and sounds attached to moving entities.

- **Fire-and-forget:** Game code calls `IAudioEngine::play()` or `play3D()` directly, ignoring the returned handle. SoLoud automatically cleans up the voice when it finishes. Used for one-shot SFX (gunshot, UI click) that do not need position updates.

Fire-and-forget does not require an ECS component. The `AudioSourceComponent` is only needed when the sound must be tracked across frames.

---

### 7. Asset Integration

#### 7.1 AudioClip Asset Type

A new `AudioClip` type in `engine/audio/AudioClip.h`:

```cpp
namespace engine::audio
{

struct AudioClip
{
    uint32_t clipId = 0;        // ID returned by IAudioEngine::loadClip()
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    float    duration = 0.0f;   // seconds
    bool     streaming = false;

    [[nodiscard]] bool isValid() const { return clipId != 0; }
};

}  // namespace engine::audio
```

#### 7.2 CpuAudioData

Added to the `CpuAssetData` variant in `engine/assets/CpuAssetData.h`:

```cpp
struct CpuAudioData
{
    std::vector<uint8_t> bytes;     // raw file bytes (WAV/OGG)
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    float    duration = 0.0f;
    bool     streaming = false;
};

using CpuAssetData = std::variant<CpuTextureData, CpuSceneData, CpuAudioData>;
```

#### 7.3 AudioClipLoader

A new `IAssetLoader` implementation in `engine/audio/AudioClipLoader.h` and `.cpp`:

- `extensions()` returns `{".wav", ".ogg", ".mp3"}`
- `decode()` parses the audio file header to extract metadata (sample rate, channels, duration). For preloaded clips, the raw bytes are stored in `CpuAudioData::bytes`. For streaming clips, only the path is stored and the file is opened on demand.
- The actual SoLoud `Wav::loadMem()` call happens during upload on the main thread, similar to how `bgfx::createTexture()` happens during `processUploads()`.

The `AssetManager` gains a new `upload(Record& rec, CpuAudioData&& data)` overload that calls `IAudioEngine::loadClip()` and stores an `AudioClip` in the record payload.

**Registration:**
```cpp
assets.registerLoader(std::make_unique<AudioClipLoader>());
```

#### 7.4 Streaming vs Preloaded

- **Preloaded (default):** Entire file decoded into memory via `SoLoud::Wav::loadMem()`. Best for short SFX (< 2 seconds, < 1MB). Allows instant playback with no I/O stalls.
- **Streaming:** File opened on demand via `SoLoud::WavStream`. Best for music and long ambient loops (> 5 seconds). Reads from disk in small chunks. The `AudioSourceComponent::flags` bit or a separate load parameter controls this.

Decision heuristic: files under 512KB are preloaded; larger files stream. This can be overridden explicitly per asset.

---

### 8. Sound Categories and Volume Control

Four categories, each backed by a `SoLoud::Bus`:

| Category | Use Case | Default Volume |
|----------|----------|---------------|
| `SFX` | Gunshots, footsteps, impacts | 1.0 |
| `Music` | Background music, score | 0.7 |
| `UI` | Button clicks, menu sounds | 0.8 |
| `Ambient` | Wind, rain, crowd noise | 0.6 |

Architecture:

```
Master Volume
  +-- SFX Bus
  +-- Music Bus
  +-- UI Bus
  +-- Ambient Bus
```

Each bus is a `SoLoud::Bus` instance played into the main `SoLoud::Soloud` mixer. When `play()` or `play3D()` is called, the sound is routed through the correct bus based on the `SoundCategory` parameter. `setCategoryVolume()` calls `bus.setVolume()`, which affects all sounds currently playing on that bus.

The Music bus typically has at most 1-2 active voices. The SFX bus handles the highest voice count. UI sounds are always 2D (non-spatial).

---

### 9. File Layout

```
engine/audio/
    IAudioEngine.h              -- abstract interface
    SoLoudAudioEngine.h         -- SoLoud implementation (header)
    SoLoudAudioEngine.cpp       -- SoLoud implementation
    NullAudioEngine.h           -- no-op implementation (for tests, headless)
    AudioComponents.h           -- AudioSourceComponent, AudioListenerComponent, SoundCategory
    AudioSystem.h               -- ECS system (header)
    AudioSystem.cpp             -- ECS system implementation
    AudioClip.h                 -- AudioClip asset type
    AudioClipLoader.h           -- IAssetLoader for .wav/.ogg/.mp3 (header)
    AudioClipLoader.cpp         -- loader implementation

tests/audio/
    TestAudioComponents.cpp     -- component layout static_asserts, construction
    TestAudioSystem.cpp         -- system logic with NullAudioEngine
    TestAudioClipLoader.cpp     -- loader decode with test WAV data
    TestCategoryVolume.cpp      -- category volume routing
    FakeAudioEngine.h           -- mock IAudioEngine for tests

third_party/soloud/            -- vendored SoLoud sources (or fetched via FetchContent)
    CMakeLists.txt              -- minimal build for core + miniaudio backend
    include/
    src/
```

The `NullAudioEngine` follows the `NullInputBackend` pattern from the input system -- a no-op implementation that allows all engine and test code to run without an audio device. Every method is a valid no-op (play returns INVALID_SOUND, isPlaying returns false, etc.).

---

### 10. Test Plan

Audio output cannot be verified programmatically. All tests focus on state management, not actual sound reproduction.

#### 10.1 TestAudioComponents.cpp `[audio]`

- `static_assert` on `sizeof(AudioSourceComponent)` == 28
- `static_assert` on `sizeof(AudioListenerComponent)` == 4
- Verify `offsetof` for each field matches documented layout
- Default-construct components and verify field defaults (volume = 1.0, pitch = 1.0, etc.)
- `SoundCategory` enum values (SFX=0, Music=1, UI=2, Ambient=3)

#### 10.2 TestAudioSystem.cpp `[audio]`

Uses `FakeAudioEngine` (records all calls for inspection, similar to `FakeInputBackend` in input tests).

- **Listener update:** Create entity with `AudioListenerComponent` + `WorldTransformComponent`. Call `AudioSystem::update()`. Verify `FakeAudioEngine` received `setListenerPosition` and `setListenerOrientation` with correct values extracted from the world matrix.
- **Source position update:** Create entity with `AudioSourceComponent` (playing) + `WorldTransformComponent`. Call `update()`. Verify `setPosition` called with world position.
- **Auto-play:** Create entity with autoPlay flag set, busHandle == 0. After `update()`, verify `play3D` was called and busHandle is now non-zero.
- **Playback completion:** Set `FakeAudioEngine::isPlaying()` to return false for a handle. After `update()`, verify busHandle reset to 0.
- **Multiple listeners:** Create two listener entities with different priorities. Verify only the highest-priority listener's transform is used.
- **No listener:** No entity with `AudioListenerComponent`. Verify `update()` does not crash and does not call `setListenerPosition`.
- **No sources:** No entity with `AudioSourceComponent`. Verify clean no-op.

#### 10.3 TestCategoryVolume.cpp `[audio]`

Uses `FakeAudioEngine`.

- Set category volumes via `setCategoryVolume()`. Verify `getCategoryVolume()` returns the set values.
- Verify master volume is independent of category volumes.
- Verify volume clamping (values outside [0,1] are clamped or rejected).

#### 10.4 TestAudioClipLoader.cpp `[audio]`

- Verify `extensions()` returns `.wav`, `.ogg`, `.mp3`.
- Feed a minimal valid WAV file (44-byte header + a few samples) to `decode()`. Verify `CpuAudioData` fields (sampleRate, channels, duration) are correct.
- Feed invalid/corrupt bytes. Verify `decode()` throws `std::runtime_error`.
- Feed empty byte span. Verify exception.

#### 10.5 TestNullAudioEngine.cpp `[audio]`

- Verify `NullAudioEngine::init()` returns true.
- Verify `play()` returns `INVALID_SOUND`.
- Verify `isPlaying()` returns false for any handle.
- Verify no crashes on any method call sequence.
- Verify `shutdown()` is idempotent.

#### 10.6 FakeAudioEngine

```cpp
class FakeAudioEngine final : public IAudioEngine
{
public:
    // Records of calls made, queryable by tests
    struct PlayCall { uint32_t clipId; math::Vec3 pos; float volume; bool loop; };
    std::vector<PlayCall> playCalls;
    std::vector<std::pair<SoundHandle, math::Vec3>> setPositionCalls;
    math::Vec3 lastListenerPos{};
    math::Vec3 lastListenerForward{};
    math::Vec3 lastListenerUp{};
    // ... configurable return values for isPlaying, etc.
};
```

---

### 11. Performance Considerations

#### 11.1 Voice Limiting

SoLoud defaults to 16 simultaneous voices but supports up to ~4096. Configure `maxVoices = 32` at init, which is sufficient for most game scenarios. SoLoud handles voice stealing internally when the limit is exceeded -- lowest-volume voices are killed first.

#### 11.2 Distance-Based Culling

AudioSystem should skip `engine_.setPosition()` calls for sources beyond `maxDistance` from the listener. This avoids updating voices that are inaudible:

```
if (distanceSquared(sourcePos, listenerPos) > src.maxDistance * src.maxDistance)
{
    if (src.busHandle != 0 && !src.loop)
    {
        engine_.stop(src.busHandle);
        src.busHandle = 0;
    }
    continue;  // skip position update
}
```

Looping sounds beyond `maxDistance` are paused rather than stopped, so they resume when the listener moves closer.

#### 11.3 Update Frequency

AudioSystem runs every frame. For 60fps, this means 3D position updates every ~16ms, which is more than sufficient for smooth panning and Doppler. If frame rate drops, audio degradation is graceful -- positions update less frequently but playback continues uninterrupted because SoLoud runs its own mixing thread.

#### 11.4 Memory Budget

- Short SFX (< 2s, 44.1kHz stereo 16-bit): ~350KB each preloaded. Budget 20 clips = ~7MB.
- Music streams: ~64KB buffer each. Budget 2 streams = ~128KB.
- SoLoud engine overhead: ~200KB.
- Total audio memory budget: ~8-10MB, well within mobile constraints.

#### 11.5 Thread Safety

SoLoud's `Soloud` object is internally thread-safe for playback calls. However, `AudioSystem::update()` should run on the main thread (matching the threading contract documented in NOTES.md: "deferred for physics, audio, input, and all non-render ECS systems"). The mixing thread is internal to SoLoud/miniaudio and does not interact with the ECS.

#### 11.6 Component Iteration Cost

`AudioSourceComponent` is 28 bytes. A view over 1000 audio sources iterates ~28KB of data -- comfortably within L1 cache. The `WorldTransformComponent` (64 bytes) is read alongside it, adding 64KB. Both are SparseSet-backed, so iteration is contiguous.

---

### 12. Initialization and Shutdown Lifecycle

```
Application startup:
    1. Renderer::init()
    2. SoLoudAudioEngine engine;
       engine.init(44100, 2048, 32);
    3. AssetManager assets(pool, fs);
       assets.registerLoader(std::make_unique<AudioClipLoader>(engine));
    4. AudioSystem audioSystem(engine);

Frame loop:
    1. inputSystem.update(inputState);
    2. // game logic (may call engine.play() for fire-and-forget)
    3. transformSystem.update(reg);
    4. audioSystem.update(reg);
    5. // rendering systems...
    6. assets.processUploads();  // includes audio clip uploads
    7. renderer.endFrame();

Application shutdown (LIFO):
    1. // AssetManager destroyed (unloads clips)
    2. engine.shutdown();  // or destructor
    3. Renderer::shutdown()
```

`IAudioEngine` must outlive `AssetManager` because `AudioClipLoader::upload()` calls `IAudioEngine::loadClip()`. This mirrors the constraint documented for `Renderer` in `AssetManager.h`: "bgfx context (Renderer) MUST still be alive when AssetManager is destroyed."

---

### 13. Future: Occlusion (from NOTES.md)

The occlusion system described in NOTES.md (Jolt raycasts between source and listener, mapped to volume reduction + low-pass filter) is not part of this initial architecture. It will be added as a separate `AudioOcclusionSystem` that runs between step 3 (TransformSystem) and step 4 (AudioSystem), writing an `occlusionFactor` field into `AudioSourceComponent`. AudioSystem then applies the factor when setting volume. This keeps occlusion decoupled and removable when FMOD is adopted.

---

### Critical Files for Implementation

- `/Users/shayanj/claude/engine/engine/audio/IAudioEngine.h` -- the abstraction interface; everything depends on this
- `/Users/shayanj/claude/engine/engine/audio/AudioComponents.h` -- ECS components (AudioSourceComponent, AudioListenerComponent, SoundCategory)
- `/Users/shayanj/claude/engine/engine/audio/AudioSystem.cpp` -- the ECS system that bridges components to IAudioEngine
- `/Users/shayanj/claude/engine/engine/assets/CpuAssetData.h` -- must be extended with CpuAudioData variant
- `/Users/shayanj/claude/engine/CMakeLists.txt` -- build integration for engine_audio target and SoLoud dependency