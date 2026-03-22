#pragma once

// GLM compile flags — documentation only.
//
// These flags are enforced via CMake target_compile_definitions so they take effect
// before any GLM header is included, regardless of include order.  (clang-format with
// IncludeBlocks:Regroup would otherwise reorder headers and cause defines to arrive
// too late.)  This header documents what flags are active and why.

// Enable SSE2/AVX intrinsics on x86/x64 (Windows/Mac desktop). Free perf, zero code changes.
#ifndef GLM_FORCE_INTRINSICS
#define GLM_FORCE_INTRINSICS
#endif

// bgfx and Vulkan use depth range [0, 1]. OpenGL uses [-1, 1].
// This flag makes GLM projection functions (perspective, ortho) output [0, 1] depth.
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

// All engine angles are in radians. Disabling degree overloads eliminates an entire
// class of degree/radian bugs where callers pass degrees to a function expecting radians.
#ifndef GLM_FORCE_RADIANS
#define GLM_FORCE_RADIANS
#endif

// Vectors represent positions and directions — use .x .y .z .w consistently.
// Disabling .r .g .b .a swizzles on Vec3/Vec4 prevents confusing positional vectors
// with colour vectors at the type level.
#ifndef GLM_FORCE_XYZW_ONLY
#define GLM_FORCE_XYZW_ONLY
#endif

// Required to use glm/gtx/ experimental headers (e.g. glm/gtx/matrix_decompose.hpp).
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
