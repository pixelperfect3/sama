# Binary Size Impact of Sama Umbrella Targets

This document measures how much binary size each of the Sama umbrella CMake
INTERFACE libraries (`sama_minimal`, `sama_3d`, `sama`) contributes to a
shipped game executable on macOS (arm64, Apple clang).

## TL;DR

| Umbrella        | Debug    | Release  | Release + `strip` |
|-----------------|---------:|---------:|------------------:|
| `sama_minimal`  |  148 KB  |  34.3 KB |           33.8 KB |
| `sama_3d`       |  148 KB  |  34.1 KB |           33.7 KB |
| `sama`          |  261 KB  |  54.2 KB |           51.8 KB |

These are **lower bounds**: what a game pays for when it pulls in only a
handful of symbols from each umbrella. The macOS linker's dead-strip aggressively
removes any object code the executable doesn't reference, so the actual size of
a real game depends entirely on how much of the API surface it touches. The
upper bound — the total size of every `.a` archive the umbrella transitively
references — is much larger (see "Upper bound" below).

## Methodology

Three tiny executables, each with a `main()` that instantiates a handful of
types from the umbrella it links to, force the linker to resolve symbols from
the relevant static archives:

- `apps/size_test/size_test_minimal.mm`  -> links `sama_minimal`
- `apps/size_test/size_test_3d.mm`       -> links `sama_3d`
- `apps/size_test/size_test_full.mm`     -> links `sama`

All three link to the same Apple frameworks (Cocoa, Metal, QuartzCore, IOKit,
CoreFoundation, AudioToolbox, CoreAudio) so framework overhead is constant and
the delta between binaries is pure "Nimbus engine weight".

Build commands:

```bash
# Debug
cmake -B build_debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build_debug --target size_test_minimal size_test_3d size_test_full

# Release
cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build_release --target size_test_minimal size_test_3d size_test_full

# Strip symbols from Release binaries (what ships)
strip -o size_test_minimal_stripped size_test_minimal
```

## Raw measurements (bytes)

| Binary                        | Debug  | Release | Release stripped |
|-------------------------------|-------:|--------:|-----------------:|
| `size_test_minimal`           | 151584 |   35088 |            34568 |
| `size_test_3d`                | 152064 |   34912 |            34488 |
| `size_test_full`              | 267696 |   55472 |            53080 |

### Deltas between umbrellas (stripped Release)

| Step                                     | Added bytes | Cumulative |
|------------------------------------------|------------:|-----------:|
| Baseline (`sama_minimal`)                |       34568 |      34568 |
| `sama_3d` over `sama_minimal`            |         -80 |      34488 |
| `sama` over `sama_3d`                    |       18592 |      53080 |

The `sama_3d` delta is essentially zero because our `main()` only touches
`AnimationSystem` (which has no runtime state beyond two pointers) and includes
headers from physics/audio/assets without referencing their implementations.
The `sama` delta of ~18 KB is all from `engine::threading::ThreadPool(2)`: two
`std::thread` worker loops, the mutex/condvar plumbing, and libc++ `std::thread`
template instantiations.

## Upper bound: full static-archive weight

This is the size the engine libraries occupy in `build_release/` before the
linker dead-strips unused code:

| Static archive              |    Bytes | Umbrella tier |
|-----------------------------|---------:|---------------|
| `libengine_ecs.a`           |     3912 | minimal       |
| `libengine_memory.a`        |     7512 | minimal       |
| `libengine_core.a`          |     9624 | minimal       |
| `libengine_game.a`          |    14160 | minimal       |
| `libengine_scene.a`         |   197104 | minimal       |
| `libengine_rendering.a`     |   344296 | minimal       |
| **sama_minimal subtotal**   |  **576608** |            |
| `libengine_animation.a`     |    60560 | 3d            |
| `libengine_physics.a`       |    94488 | 3d            |
| `libengine_assets.a`        |   775368 | 3d            |
| `libengine_audio.a`         |   974304 | 3d            |
| **sama_3d additional**      | **1904720** |            |
| `libengine_threading.a`     |    16392 | full          |
| `libengine_input.a`         |    37376 | full          |
| `libengine_io.a`            |    70128 | full          |
| **sama full additional**    |  **123896** |            |

Third-party statically-linked archives dwarf the engine code itself:

| Third-party archive | Bytes | Pulled in by |
|---------------------|------:|--------------|
| `libJolt.a`         | 90.7 MB | `sama_3d` (physics) |
| `libspirv-tools.a`  | 11.5 MB | `sama_minimal` (bgfx shader runtime) |
| `libspirv-cross.a`  |  4.7 MB | `sama_minimal` (bgfx) |
| `libglslang.a`      |  4.4 MB | `sama_minimal` (bgfx) |
| `libglsl-optimizer.a` | 1.8 MB | `sama_minimal` (bgfx) |
| `libbgfx.a`         |  1.2 MB | `sama_minimal` (rendering) |
| `libglm.a`          |  640 KB | all                     |
| `libbimg.a`         |  643 KB | `sama_minimal` (textures)|
| `libglfw3.a`        |  290 KB | `sama` (via input_glfw)  |

## Observations

1. **Dead-strip is extraordinarily effective.** A game that only uses a narrow
   slice of the engine API pays ~35 KB for `sama_minimal` even though the
   underlying archives total ~576 KB of engine code plus ~24 MB of third-party
   shader-compiler archives. The linker discards everything unreferenced.

2. **Release strips ~77 percent off Debug.** The Debug->Release step drops
   `size_test_minimal` from 148 KB to 34 KB — most of Debug weight is
   unreachable `assert`/`std::string` debug iterator code and unstripped
   symbols the Release build's `-O2` + dead-strip removes.

3. **`strip` buys you another 1–4 percent** on top of Release. Most of the
   Release symbol table is already consumed by `-dead_strip`. The extra
   `strip -o` pass trims a few KB of remaining `__LINKEDIT` metadata.

4. **Header inclusion is free.** `size_test_3d` pulls in `IPhysicsEngine.h`,
   `IAudioEngine.h`, and `AssetManager.h` but doesn't instantiate any of those
   classes, so Jolt, SoLoud, cgltf, etc. never make it into the binary. The
   expensive step is not "link to `sama_3d`" but "call `createJoltPhysicsEngine()`".

5. **Worst-case growth from adding `sama_3d` to a minimal game is huge if
   physics is actually used.** A real game that instantiates the Jolt physics
   engine will pull in megabytes of `libJolt.a` code. The same applies to
   SoLoud + miniaudio from `engine_audio` (~1 MB of archive), and cgltf + stb
   from `engine_assets` (~775 KB).

6. **`sama` over `sama_3d` is cheap by default.** The three libraries added
   (`engine_threading`, `engine_input`, `engine_io`) total ~124 KB of archive,
   and none of them have heavy transitive dependencies. Baseline cost of just
   adopting `sama` over `sama_3d` is ~19 KB in stripped Release.

## When to pick which umbrella

- Pick **`sama_minimal`** for UI tools, editors, non-gameplay viewers, or
  prototypes that only need rendering + scene graph + ECS.
- Pick **`sama_3d`** for most games. You get physics, audio, animation, and
  asset loading without paying for any of them until you actually call into
  their runtimes.
- Pick **`sama`** when you need threading primitives, platform input, or JSON
  I/O in the game layer. On iOS/console builds you can often avoid this by
  relying on the platform's native input integration.

## Reproducing

```bash
# (from repo root, with a fresh checkout)
cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build_release --target size_test_minimal size_test_3d size_test_full
cd build_release && strip -o m size_test_minimal && strip -o t size_test_3d && strip -o f size_test_full && ls -l m t f
```
