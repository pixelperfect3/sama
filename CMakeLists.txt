cmake_minimum_required(VERSION 3.20)
project(Engine C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Engine interface library (header-only core)
add_library(engine INTERFACE)
target_include_directories(engine INTERFACE ${CMAKE_SOURCE_DIR})

# Dependencies
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.4.0
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.9
)

# bgfx.cmake wraps bgfx/bx/bimg with proper CMake targets.
# We disable all backends except Metal (macOS) and Vulkan.
set(BGFX_BUILD_TOOLS      ON  CACHE BOOL "" FORCE) # build shaderc
set(BGFX_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL          OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS   OFF CACHE BOOL "" FORCE)

# Disable unused backends to keep the binary lean
set(BGFX_USE_DEBUG_SUFFIX OFF CACHE BOOL "" FORCE)

# GLFW: disable unneeded sub-targets
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    bgfx_cmake
    GIT_REPOSITORY https://github.com/widberg/bgfx.cmake.git
    GIT_TAG        eebff97af2a34c695cdc058c63ba527c6ebebc76
)

# cgltf — single-header glTF 2.0 parser (MIT).
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.14
)

# Jolt Physics — rigid body physics engine.
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.2.0
    SOURCE_SUBDIR  Build
)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Catch2 glm glfw bgfx_cmake cgltf JoltPhysics)

# stb — header-only, used by engine_assets (TextureLoader) and screenshot tests.
# FetchContent_Populate avoids creating unused CMake targets for a header-only lib.
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# SoLoud — game audio engine (MIT). Header-only FetchContent_Populate like stb;
# we compile the needed sources into engine_audio below.
FetchContent_Declare(
    soloud
    GIT_REPOSITORY https://github.com/jarikomppa/soloud.git
    GIT_TAG        RELEASE_20200207
)
FetchContent_GetProperties(soloud)
if(NOT soloud_POPULATED)
    FetchContent_Populate(soloud)
endif()
set(SOLOUD_SRC_DIR ${soloud_SOURCE_DIR}/src)
set(SOLOUD_INC_DIR ${soloud_SOURCE_DIR}/include)

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

# ---------------------------------------------------------------------------
# Shader compilation — runs shaderc at build time, embeds bytecode as .h files
# ---------------------------------------------------------------------------

set(SHADER_SRC_DIR     ${CMAKE_SOURCE_DIR}/engine/shaders)
set(SHADER_INCLUDE_DIR ${bgfx_cmake_SOURCE_DIR}/bgfx/src)
set(SHADER_OUT_DIR     ${CMAKE_CURRENT_BINARY_DIR}/include/generated/shaders)
set(VARYING_DEF        ${SHADER_SRC_DIR}/varying.def.sc)
set(VARYING_PP_DEF     ${SHADER_SRC_DIR}/varying_pp.def.sc)

# Helper: compile one shader via shaderc --bin2c and append its output header
# to a list variable.
#
# Arguments:
#   FILE       — shader source path
#   TYPE       — VERTEX or FRAGMENT
#   PLATFORM   — OSX or LINUX
#   PROFILE    — metal or spirv
#   BIN2C_NAME — name of the C symbol (also used as part of output filename)
#   OUT_LIST   — CMake variable to append the output path to
function(compile_shader FILE TYPE PLATFORM PROFILE BIN2C_NAME OUT_LIST)
    set(OUT_FILE ${SHADER_OUT_DIR}/${BIN2C_NAME}.bin.h)

    # Build the shaderc_parse argument list; PROFILE is optional (empty = omit).
    set(_PARSE_ARGS
        FILE       ${FILE}
        OUTPUT     ${OUT_FILE}
        BIN2C      ${BIN2C_NAME}
        ${PLATFORM}
        ${TYPE}
        VARYINGDEF ${VARYING_DEF}
        INCLUDES   ${SHADER_INCLUDE_DIR}
    )
    if(NOT "${PROFILE}" STREQUAL "")
        list(APPEND _PARSE_ARGS PROFILE ${PROFILE})
    endif()

    shaderc_parse(CLI ${_PARSE_ARGS})

    add_custom_command(
        OUTPUT  ${OUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
        COMMAND $<TARGET_FILE:shaderc> ${CLI}
        MAIN_DEPENDENCY ${FILE}
        DEPENDS ${VARYING_DEF} shaderc
        COMMENT "Compiling shader ${BIN2C_NAME}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set(${OUT_LIST} ${${OUT_LIST}} ${OUT_FILE} PARENT_SCOPE)
endfunction()

# Variant that uses the post-process varying def (varying_pp.def.sc).
# Identical to compile_shader() but references VARYING_PP_DEF.
function(compile_shader_pp FILE TYPE PLATFORM PROFILE BIN2C_NAME OUT_LIST)
    set(OUT_FILE ${SHADER_OUT_DIR}/${BIN2C_NAME}.bin.h)

    set(_PARSE_ARGS
        FILE       ${FILE}
        OUTPUT     ${OUT_FILE}
        BIN2C      ${BIN2C_NAME}
        ${PLATFORM}
        ${TYPE}
        VARYINGDEF ${VARYING_PP_DEF}
        INCLUDES   ${SHADER_INCLUDE_DIR}
    )
    if(NOT "${PROFILE}" STREQUAL "")
        list(APPEND _PARSE_ARGS PROFILE ${PROFILE})
    endif()

    shaderc_parse(CLI ${_PARSE_ARGS})

    add_custom_command(
        OUTPUT  ${OUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
        COMMAND $<TARGET_FILE:shaderc> ${CLI}
        MAIN_DEPENDENCY ${FILE}
        DEPENDS ${VARYING_PP_DEF} shaderc
        COMMENT "Compiling shader ${BIN2C_NAME}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set(${OUT_LIST} ${${OUT_LIST}} ${OUT_FILE} PARENT_SCOPE)
endfunction()

set(COMPILED_SHADERS "")

# Unlit — Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_unlit.sc VERTEX   OSX   metal  vs_unlit_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_unlit.sc FRAGMENT OSX   metal  fs_unlit_mtl  COMPILED_SHADERS)

# Unlit — SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/vs_unlit.sc VERTEX   LINUX spirv  vs_unlit_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_unlit.sc FRAGMENT LINUX spirv  fs_unlit_spv  COMPILED_SHADERS)

# Unlit — GLSL (OpenGL; required by BGFX_EMBEDDED_SHADER on macOS/Linux)
compile_shader(
    ${SHADER_SRC_DIR}/vs_unlit.sc VERTEX   LINUX 120    vs_unlit_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_unlit.sc FRAGMENT LINUX 120    fs_unlit_glsl COMPILED_SHADERS)

# Unlit — ESSL (OpenGLES / Android; required by BGFX_EMBEDDED_SHADER on macOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_unlit.sc VERTEX   ANDROID ""   vs_unlit_essl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_unlit.sc FRAGMENT ANDROID ""   fs_unlit_essl COMPILED_SHADERS)

# PBR — Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_pbr.sc VERTEX   OSX   metal  vs_pbr_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_pbr.sc FRAGMENT OSX   metal  fs_pbr_mtl  COMPILED_SHADERS)

# PBR — SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/vs_pbr.sc VERTEX   LINUX spirv  vs_pbr_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_pbr.sc FRAGMENT LINUX spirv  fs_pbr_spv  COMPILED_SHADERS)

# PBR — GLSL (OpenGL; required by BGFX_EMBEDDED_SHADER on macOS/Linux)
compile_shader(
    ${SHADER_SRC_DIR}/vs_pbr.sc VERTEX   LINUX 120    vs_pbr_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_pbr.sc FRAGMENT LINUX 120    fs_pbr_glsl COMPILED_SHADERS)

# PBR — ESSL (OpenGLES / Android; required by BGFX_EMBEDDED_SHADER on macOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_pbr.sc VERTEX   ANDROID ""   vs_pbr_essl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_pbr.sc FRAGMENT ANDROID ""   fs_pbr_essl COMPILED_SHADERS)

# PBR Skinned — Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_pbr_skinned.sc VERTEX   OSX   metal  vs_pbr_skinned_mtl  COMPILED_SHADERS)

# PBR Skinned — SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/vs_pbr_skinned.sc VERTEX   LINUX spirv  vs_pbr_skinned_spv  COMPILED_SHADERS)

# PBR Skinned — GLSL (OpenGL)
compile_shader(
    ${SHADER_SRC_DIR}/vs_pbr_skinned.sc VERTEX   LINUX 120    vs_pbr_skinned_glsl COMPILED_SHADERS)

# PBR Skinned — ESSL (OpenGLES / Android)
compile_shader(
    ${SHADER_SRC_DIR}/vs_pbr_skinned.sc VERTEX   ANDROID ""   vs_pbr_skinned_essl COMPILED_SHADERS)

# Instanced — Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_instanced.sc VERTEX   OSX     metal  vs_instanced_mtl  COMPILED_SHADERS)

# Instanced — SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/vs_instanced.sc VERTEX   LINUX   spirv  vs_instanced_spv  COMPILED_SHADERS)

# Instanced — GLSL (OpenGL)
compile_shader(
    ${SHADER_SRC_DIR}/vs_instanced.sc VERTEX   LINUX   120    vs_instanced_glsl COMPILED_SHADERS)

# Instanced — ESSL (OpenGLES / Android)
compile_shader(
    ${SHADER_SRC_DIR}/vs_instanced.sc VERTEX   ANDROID ""     vs_instanced_essl COMPILED_SHADERS)

# Sprite — Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_sprite.sc VERTEX   OSX     metal  vs_sprite_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_sprite.sc FRAGMENT OSX     metal  fs_sprite_mtl  COMPILED_SHADERS)

# Sprite — SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/vs_sprite.sc VERTEX   LINUX   spirv  vs_sprite_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_sprite.sc FRAGMENT LINUX   spirv  fs_sprite_spv  COMPILED_SHADERS)

# Sprite — GLSL (OpenGL; required by BGFX_EMBEDDED_SHADER on macOS/Linux)
compile_shader(
    ${SHADER_SRC_DIR}/vs_sprite.sc VERTEX   LINUX   120    vs_sprite_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_sprite.sc FRAGMENT LINUX   120    fs_sprite_glsl COMPILED_SHADERS)

# Sprite — ESSL (OpenGLES / Android; required by BGFX_EMBEDDED_SHADER on macOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_sprite.sc VERTEX   ANDROID ""     vs_sprite_essl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_sprite.sc FRAGMENT ANDROID ""     fs_sprite_essl COMPILED_SHADERS)

# Shadow — Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_shadow.sc VERTEX   OSX     metal  vs_shadow_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_shadow.sc FRAGMENT OSX     metal  fs_shadow_mtl  COMPILED_SHADERS)

# Shadow — SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/vs_shadow.sc VERTEX   LINUX   spirv  vs_shadow_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_shadow.sc FRAGMENT LINUX   spirv  fs_shadow_spv  COMPILED_SHADERS)

# Shadow — GLSL (OpenGL; required by BGFX_EMBEDDED_SHADER on macOS/Linux)
compile_shader(
    ${SHADER_SRC_DIR}/vs_shadow.sc VERTEX   LINUX   120    vs_shadow_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_shadow.sc FRAGMENT LINUX   120    fs_shadow_glsl COMPILED_SHADERS)

# Shadow — ESSL (OpenGLES / Android; required by BGFX_EMBEDDED_SHADER on macOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_shadow.sc VERTEX   ANDROID ""     vs_shadow_essl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_shadow.sc FRAGMENT ANDROID ""     fs_shadow_essl COMPILED_SHADERS)

# Shadow Skinned — Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_shadow_skinned.sc VERTEX   OSX     metal  vs_shadow_skinned_mtl  COMPILED_SHADERS)

# Shadow Skinned — SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/vs_shadow_skinned.sc VERTEX   LINUX   spirv  vs_shadow_skinned_spv  COMPILED_SHADERS)

# Shadow Skinned — GLSL (OpenGL)
compile_shader(
    ${SHADER_SRC_DIR}/vs_shadow_skinned.sc VERTEX   LINUX   120    vs_shadow_skinned_glsl COMPILED_SHADERS)

# Shadow Skinned — ESSL (OpenGLES / Android)
compile_shader(
    ${SHADER_SRC_DIR}/vs_shadow_skinned.sc VERTEX   ANDROID ""     vs_shadow_skinned_essl COMPILED_SHADERS)

# Post-process fullscreen vertex × 4 platforms
compile_shader_pp(
    ${SHADER_SRC_DIR}/vs_fullscreen.sc VERTEX OSX     metal vs_fullscreen_mtl  COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/vs_fullscreen.sc VERTEX LINUX   spirv vs_fullscreen_spv  COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/vs_fullscreen.sc VERTEX LINUX   120   vs_fullscreen_glsl COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/vs_fullscreen.sc VERTEX ANDROID ""    vs_fullscreen_essl COMPILED_SHADERS)

# Bloom threshold
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_threshold.sc FRAGMENT OSX     metal  fs_bloom_threshold_mtl  COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_threshold.sc FRAGMENT LINUX   spirv  fs_bloom_threshold_spv  COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_threshold.sc FRAGMENT LINUX   120    fs_bloom_threshold_glsl COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_threshold.sc FRAGMENT ANDROID ""     fs_bloom_threshold_essl COMPILED_SHADERS)

# Bloom downsample
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_downsample.sc FRAGMENT OSX     metal  fs_bloom_downsample_mtl  COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_downsample.sc FRAGMENT LINUX   spirv  fs_bloom_downsample_spv  COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_downsample.sc FRAGMENT LINUX   120    fs_bloom_downsample_glsl COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_downsample.sc FRAGMENT ANDROID ""     fs_bloom_downsample_essl COMPILED_SHADERS)

# Bloom upsample
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_upsample.sc FRAGMENT OSX     metal  fs_bloom_upsample_mtl  COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_upsample.sc FRAGMENT LINUX   spirv  fs_bloom_upsample_spv  COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_upsample.sc FRAGMENT LINUX   120    fs_bloom_upsample_glsl COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_bloom_upsample.sc FRAGMENT ANDROID ""     fs_bloom_upsample_essl COMPILED_SHADERS)

# Tonemap
compile_shader_pp(${SHADER_SRC_DIR}/fs_tonemap.sc FRAGMENT OSX     metal  fs_tonemap_mtl  COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_tonemap.sc FRAGMENT LINUX   spirv  fs_tonemap_spv  COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_tonemap.sc FRAGMENT LINUX   120    fs_tonemap_glsl COMPILED_SHADERS)
compile_shader_pp(${SHADER_SRC_DIR}/fs_tonemap.sc FRAGMENT ANDROID ""     fs_tonemap_essl COMPILED_SHADERS)

# FXAA × 4 platforms
compile_shader_pp(
    ${SHADER_SRC_DIR}/fs_fxaa.sc FRAGMENT OSX     metal fs_fxaa_mtl  COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/fs_fxaa.sc FRAGMENT LINUX   spirv fs_fxaa_spv  COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/fs_fxaa.sc FRAGMENT LINUX   120   fs_fxaa_glsl COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/fs_fxaa.sc FRAGMENT ANDROID ""    fs_fxaa_essl COMPILED_SHADERS)

# SSAO × 4 platforms
compile_shader_pp(
    ${SHADER_SRC_DIR}/fs_ssao.sc FRAGMENT OSX     metal fs_ssao_mtl  COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/fs_ssao.sc FRAGMENT LINUX   spirv fs_ssao_spv  COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/fs_ssao.sc FRAGMENT LINUX   120   fs_ssao_glsl COMPILED_SHADERS)
compile_shader_pp(
    ${SHADER_SRC_DIR}/fs_ssao.sc FRAGMENT ANDROID ""    fs_ssao_essl COMPILED_SHADERS)

add_custom_target(engine_shaders ALL DEPENDS ${COMPILED_SHADERS})

# --- engine_io static library ---
add_library(engine_io STATIC engine/io/Json.cpp)
target_include_directories(engine_io PUBLIC ${CMAKE_SOURCE_DIR} ${rapidjson_SOURCE_DIR}/include)
target_link_libraries(engine_io PUBLIC engine glm::glm)
# rapidjson v1.1.0 emits warnings under modern C++20 compilers — suppress them.
target_compile_options(engine_io PRIVATE -Wno-deprecated-declarations)

# --- engine_memory static library ---
add_library(engine_memory STATIC engine/memory/FrameArena.cpp)
target_include_directories(engine_memory PUBLIC ${CMAKE_SOURCE_DIR})

# --- IK compile-time solver toggles ---
option(ENGINE_IK_ENABLE_CCD "Enable CCD IK solver" ON)
option(ENGINE_IK_ENABLE_FABRIK "Enable FABRIK IK solver" ON)

# --- engine_animation static library ---
add_library(engine_animation STATIC
    engine/animation/AnimationResources.cpp
    engine/animation/AnimationSampler.cpp
    engine/animation/AnimationSystem.cpp
    engine/animation/IkSolvers.cpp
    engine/animation/IkSystem.cpp
    engine/animation/FootIkHelper.cpp
)
target_include_directories(engine_animation PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_animation PUBLIC engine_ecs glm::glm)
target_compile_definitions(engine_animation PUBLIC
    ENGINE_IK_ENABLE_CCD=$<BOOL:${ENGINE_IK_ENABLE_CCD}>
    ENGINE_IK_ENABLE_FABRIK=$<BOOL:${ENGINE_IK_ENABLE_FABRIK}>
)

# --- engine_threading static library ---
add_library(engine_threading STATIC engine/threading/ThreadPool.cpp)
target_include_directories(engine_threading PUBLIC ${CMAKE_SOURCE_DIR})

# --- engine_debug static library ---
# Bundles dear-imgui + bgfx imgui rendering backend + DebugTexturePanel.
# Any sample app that wants an ImGui overlay just links engine_debug and calls
# imguiCreate/BeginFrame/EndFrame (declared in bgfx's examples/common/imgui/imgui.h,
# which becomes available via the PUBLIC include dirs below).
set(_BGFX_IMGUI_DIR ${bgfx_cmake_SOURCE_DIR}/bgfx/examples/common/imgui)
set(_DEAR_IMGUI_DIR ${bgfx_cmake_SOURCE_DIR}/bgfx/3rdparty/dear-imgui)

add_library(engine_debug STATIC
    ${_BGFX_IMGUI_DIR}/imgui.cpp          # bgfx imgui render backend (embedded shaders)
    ${_DEAR_IMGUI_DIR}/imgui.cpp          # dear-imgui core
    ${_DEAR_IMGUI_DIR}/imgui_draw.cpp     # dear-imgui draw list
    ${_DEAR_IMGUI_DIR}/imgui_widgets.cpp  # dear-imgui widgets
    engine/debug/DebugTexturePanel.cpp
)

target_include_directories(engine_debug PUBLIC
    ${CMAKE_SOURCE_DIR}
    # <dear-imgui/imgui.h> and <iconfontheaders/...> used by bgfx's imgui.h wrapper.
    ${bgfx_cmake_SOURCE_DIR}/bgfx/3rdparty
    # imgui.h (bgfx wrapper) declares imguiCreate/BeginFrame/EndFrame — expose to callers.
    ${_BGFX_IMGUI_DIR}
)

target_link_libraries(engine_debug PUBLIC bgfx bx)

# --- engine_assets static library ---
add_library(engine_assets STATIC
    engine/assets/AssetManager.cpp
    engine/assets/StdFileSystem.cpp
    engine/assets/TextureLoader.cpp
    engine/assets/GltfLoader.cpp
    engine/assets/GltfSceneSpawner.cpp
    engine/assets/ObjLoader.cpp
    engine/assets/CompressedTextureLoader.cpp
)

target_include_directories(engine_assets PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(engine_assets PRIVATE
    ${stb_SOURCE_DIR}
    ${cgltf_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/tinyobjloader
)
target_link_libraries(engine_assets PUBLIC engine_rendering engine_threading engine_scene engine_animation glm::glm)

# --- engine_ecs static library ---
add_library(engine_ecs STATIC engine/ecs/Registry.cpp)
target_include_directories(engine_ecs PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/third_party)

# --- engine_scene static library ---
add_library(engine_scene STATIC
    engine/scene/SceneGraph.cpp
    engine/scene/TransformSystem.cpp
    engine/scene/SceneSerializer.cpp
)
target_include_directories(engine_scene PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_scene PUBLIC engine_ecs engine_io engine_rendering glm::glm)

# --- engine_audio static library ---
# Compiles SoLoud core + miniaudio backend + engine audio code into a single
# static library. Follows the same pattern as engine_assets (vendored sources
# compiled directly into the target).
add_library(engine_audio STATIC
    engine/audio/AudioSystem.cpp
    engine/audio/SoLoudAudioEngine.cpp
    # SoLoud core
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
    ${SOLOUD_SRC_DIR}/core/soloud_fader.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_file.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_filter.cpp
    ${SOLOUD_SRC_DIR}/core/soloud_thread.cpp
    # SoLoud WAV audio source + stb_vorbis
    ${SOLOUD_SRC_DIR}/audiosource/wav/soloud_wav.cpp
    ${SOLOUD_SRC_DIR}/audiosource/wav/soloud_wavstream.cpp
    ${SOLOUD_SRC_DIR}/audiosource/wav/stb_vorbis.c
    ${SOLOUD_SRC_DIR}/audiosource/wav/dr_impl.cpp
    # SoLoud miniaudio backend — patched for miniaudio v0.11 API compatibility.
    # The vendored SoLoud (2020) ships miniaudio v0.9.10 which fails on macOS 14+.
    # We replace it with miniaudio v0.11 and patch the two renamed fields.
    ${CMAKE_SOURCE_DIR}/third_party/soloud_patches/soloud_miniaudio.cpp
)

target_compile_definitions(engine_audio PRIVATE WITH_MINIAUDIO)
target_include_directories(engine_audio PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(engine_audio PUBLIC ${SOLOUD_INC_DIR})
# Use our patched miniaudio.h (v0.11) instead of SoLoud's bundled v0.9.
target_include_directories(engine_audio BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/third_party/soloud_patches)
target_link_libraries(engine_audio PUBLIC engine_ecs engine_scene glm::glm)

if(APPLE)
    target_link_libraries(engine_audio PUBLIC
        "-framework AudioToolbox"
        "-framework CoreAudio"
    )
endif()

# --- engine_physics static library ---
add_library(engine_physics STATIC
    engine/physics/JoltPhysicsEngine.cpp
    engine/physics/JoltLayerConfig.cpp
    engine/physics/JoltContactListener.cpp
    engine/physics/PhysicsSystem.cpp
)
target_include_directories(engine_physics PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_physics PUBLIC engine_ecs engine_scene Jolt glm::glm)

# engine_physics needs the same GLM flags as the rest of the engine.
target_compile_definitions(engine_physics PUBLIC
    GLM_FORCE_INTRINSICS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_XYZW_ONLY
    GLM_ENABLE_EXPERIMENTAL
)

# --- engine_rendering static library ---
# Wraps bgfx init/shutdown/frame and the GLFW window backend.
add_library(engine_rendering STATIC
    engine/rendering/Renderer.cpp
    engine/rendering/RenderPass.cpp
    engine/rendering/GpuFeatures.cpp
    engine/rendering/RenderSettings.cpp
    engine/rendering/MeshBuilder.cpp
    engine/rendering/RenderResources.cpp
    engine/rendering/ShaderLoader.cpp
    engine/rendering/ShaderUniforms.cpp
    engine/rendering/IblResources.cpp
    engine/rendering/ShadowRenderer.cpp
    engine/rendering/CsmSplitCalculator.cpp
    engine/rendering/systems/FrustumCullSystem.cpp
    engine/rendering/systems/DrawCallBuildSystem.cpp
    engine/rendering/systems/InstanceBufferBuildSystem.cpp
    engine/rendering/systems/ShadowCullSystem.cpp
    engine/rendering/systems/PostProcessSystem.cpp
    engine/rendering/systems/SsaoSystem.cpp
    engine/rendering/PostProcessResources.cpp
    engine/rendering/SpriteBatcher.cpp
    engine/rendering/systems/UiRenderSystem.cpp
    engine/rendering/RenderSettingsJson.cpp
    engine/rendering/LightClusterBuilder.cpp
    engine/platform/desktop/GlfwWindow.cpp
)

add_dependencies(engine_rendering engine_shaders)

target_include_directories(engine_rendering PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries(engine_rendering PUBLIC bgfx bx bimg glfw glm::glm engine_ecs engine_io)

# Increase bgfx bone matrix limit from default 32 to 128 for skeletal animation.
# u_model[128] = 512 vec4 uniforms, fits in BGFX_CONFIG_MAX_UNIFORMS (512).
target_compile_definitions(bgfx PUBLIC BGFX_CONFIG_MAX_BONES=128)

# Same GLM flags as engine_tests to ensure consistent behaviour when rendering
# code includes GLM headers directly.
target_compile_definitions(engine_rendering PUBLIC
    GLM_FORCE_INTRINSICS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_XYZW_ONLY
    GLM_ENABLE_EXPERIMENTAL
)

if(APPLE)
    target_link_libraries(engine_rendering PUBLIC
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
endif()

# --- engine_tests executable ---
add_executable(engine_tests
    tests/ecs/TestEntity.cpp
    tests/ecs/TestSparseSet.cpp
    tests/ecs/TestRegistry.cpp
    tests/ecs/TestView.cpp
    tests/ecs/TestSchedule.cpp
    tests/threading/TestThreadPool.cpp
    tests/math/TestMath.cpp
    tests/math/TestFrustum.cpp
    tests/math/TestTransform.cpp
    tests/rendering/TestRenderer.cpp
    tests/rendering/TestRenderSettings.cpp
    tests/rendering/TestRenderSettingsJson.cpp
    tests/rendering/TestMesh.cpp
    tests/rendering/TestInstancing.cpp
    tests/rendering/TestUi.cpp
    tests/rendering/TestPbr.cpp
    tests/rendering/TestShadow.cpp
    tests/rendering/TestCsm.cpp
    tests/rendering/TestIbl.cpp
    tests/rendering/TestLighting.cpp
    tests/rendering/TestPostProcess.cpp
    tests/rendering/TestSsao.cpp
    tests/assets/TestAssetHandle.cpp
    tests/assets/TestAssetManager.cpp
    tests/assets/TestStdFileSystem.cpp
    tests/assets/TestGltfLoader.cpp
    tests/assets/TestObjLoader.cpp
    tests/scene/TestSceneGraph.cpp
    tests/scene/TestTransformSystem.cpp
    tests/physics/TestPhysicsComponents.cpp
    tests/physics/TestPhysicsSystem.cpp
    tests/physics/TestRaycasting.cpp
    tests/physics/TestCollisionEvents.cpp
    tests/audio/TestAudioComponents.cpp
    tests/audio/TestAudioSystem.cpp
    tests/audio/TestNullAudioEngine.cpp
    tests/memory/TestFrameArena.cpp
    tests/memory/TestInlinedVector.cpp
    tests/memory/TestPoolAllocator.cpp
    tests/io/TestJson.cpp
    tests/scene/TestSceneSerializer.cpp
    tests/animation/TestAnimationComponents.cpp
    tests/animation/TestSkeleton.cpp
    tests/animation/TestAnimationSampler.cpp
    tests/animation/TestPoseBlend.cpp
    tests/animation/TestIkSolvers.cpp
)

target_link_libraries(engine_tests PRIVATE engine engine_threading engine_memory engine_io engine_assets engine_rendering engine_scene engine_physics engine_audio engine_animation glm::glm Catch2::Catch2WithMain)

target_include_directories(engine_tests PRIVATE ${stb_SOURCE_DIR})

target_compile_definitions(engine_tests PRIVATE
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# GLM compile flags — set here so they take effect before any GLM header is included,
# regardless of include order inside translation units.
# Config.h documents these flags; CMake enforces them.
target_compile_definitions(engine_tests PRIVATE
    GLM_FORCE_INTRINSICS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_XYZW_ONLY
    GLM_ENABLE_EXPERIMENTAL    # required for glm/gtx/ headers (e.g. matrix_decompose)
)

# clang-tidy integration (optional, enable with -DENABLE_CLANG_TIDY=ON)
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set_target_properties(engine_tests PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    endif()
endif()

# --- DamagedHelmet.glb — download once at configure time ---
set(HELMET_GLB "${CMAKE_SOURCE_DIR}/assets/DamagedHelmet.glb")
if(NOT EXISTS "${HELMET_GLB}")
    message(STATUS "Downloading DamagedHelmet.glb...")
    file(DOWNLOAD
        "https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb"
        "${HELMET_GLB}"
        SHOW_PROGRESS
        STATUS HELMET_DL_STATUS
    )
    list(GET HELMET_DL_STATUS 0 HELMET_DL_CODE)
    if(NOT HELMET_DL_CODE EQUAL 0)
        message(WARNING "DamagedHelmet.glb download failed — helmet_demo and helmet screenshot test will not work.")
        set(HELMET_GLB "")
    endif()
endif()

# --- engine_screenshot_tests executable ---
add_executable(engine_screenshot_tests
    tests/screenshot/main.cpp
    tests/screenshot/ScreenshotFixture.cpp
    tests/screenshot/GoldenCompare.cpp
    tests/screenshot/TestSsUnlit.cpp
    tests/screenshot/TestSsPbr.cpp
    tests/screenshot/TestSsShadow.cpp
    tests/screenshot/TestSsInstancing.cpp
    tests/screenshot/TestSsLighting.cpp
    tests/screenshot/TestSsPostProcess.cpp
    tests/screenshot/TestSsIbl.cpp
    tests/screenshot/TestSsSsao.cpp
    tests/screenshot/TestSsCsm.cpp
    tests/screenshot/TestSsUi.cpp
    tests/screenshot/TestSsHelmet.cpp
    tests/screenshot/TestSsIk.cpp
)

target_link_libraries(engine_screenshot_tests PRIVATE
    engine
    engine_assets
    engine_rendering
    glm::glm
    glfw
    Catch2::Catch2   # NOT Catch2WithMain — we supply our own main
)

target_include_directories(engine_screenshot_tests PRIVATE
    ${stb_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tests/screenshot
)

target_compile_definitions(engine_screenshot_tests PRIVATE
    GLM_FORCE_INTRINSICS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_XYZW_ONLY
    GLM_ENABLE_EXPERIMENTAL
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(APPLE)
    target_link_libraries(engine_screenshot_tests PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
endif()

# --- engine_input static library ---
add_library(engine_input STATIC
    engine/input/ActionMap.cpp
    engine/input/ActionMapJson.cpp
    engine/input/InputState.cpp
    engine/input/InputSystem.cpp
)

target_include_directories(engine_input PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_input PUBLIC engine engine_io)

# Desktop GLFW backend
add_library(engine_input_glfw STATIC
    engine/input/desktop/GlfwInputBackend.cpp
)
target_include_directories(engine_input_glfw PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_input_glfw PUBLIC engine_input glfw)

# iOS backend — .mm requires Objective-C++ compilation; only built on iOS.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_library(engine_input_ios STATIC
        engine/input/ios/IosInputBackend.mm
    )
    target_include_directories(engine_input_ios PUBLIC ${CMAKE_SOURCE_DIR})
    target_link_libraries(engine_input_ios PUBLIC engine_input)
    target_link_libraries(engine_input_ios PUBLIC "-framework UIKit")
endif()

# Android backend — only built when targeting Android.
if(ANDROID)
    add_library(engine_input_android STATIC
        engine/input/android/AndroidInputBackend.cpp
    )
    target_include_directories(engine_input_android PUBLIC ${CMAKE_SOURCE_DIR})
    target_link_libraries(engine_input_android PUBLIC engine_input android)
endif()

# --- engine_input_tests executable ---
add_executable(engine_input_tests
    tests/input/TestActionMap.cpp
    tests/input/TestActionMapJson.cpp
    tests/input/TestInputState.cpp
    tests/input/TestInputSystemTransitions.cpp
    tests/input/TestAxisResolution.cpp
    tests/input/TestMouseDelta.cpp
    tests/input/TestTouchInput.cpp
    tests/input/integration/TestGlfwBackend.cpp
)

target_include_directories(engine_input_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_input_tests PRIVATE
    engine_input
    engine_input_glfw
    Catch2::Catch2WithMain
)

target_compile_definitions(engine_input_tests PRIVATE
    ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# --- engine_core static library ---
# Centralised Engine class that owns window, renderer, default textures, shaders,
# ImGui, input, shadow renderer, and frame arena.  Apps link engine_core instead
# of manually wiring all subsystems.
add_library(engine_core STATIC engine/core/Engine.cpp)
target_include_directories(engine_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_core PUBLIC
    engine_rendering
    engine_input_glfw
    engine_memory
    engine_debug
    engine_scene
    glm::glm
)

# --- helmet_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(helmet_demo apps/helmet_demo/main.mm)

    target_include_directories(helmet_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(helmet_demo PRIVATE
        engine_core
        engine_assets
        engine_debug
        glm::glm
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(helmet_demo PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )

    # Copy the GLB next to the binary so StdFileSystem(".")/load("DamagedHelmet.glb") finds it.
    if(HELMET_GLB)
        add_custom_command(TARGET helmet_demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${HELMET_GLB}"
                "$<TARGET_FILE_DIR:helmet_demo>/DamagedHelmet.glb"
            COMMENT "Copying DamagedHelmet.glb to build directory"
        )
    endif()
endif()

# --- hierarchy_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(hierarchy_demo apps/hierarchy_demo/main.mm)

    target_include_directories(hierarchy_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(hierarchy_demo PRIVATE
        engine_core
        engine_scene
        glm::glm
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(hierarchy_demo PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )
endif()

# --- physics_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(physics_demo apps/physics_demo/main.mm)

    target_include_directories(physics_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(physics_demo PRIVATE
        engine_core
        engine_scene
        engine_physics
        glm::glm
        Jolt
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(physics_demo PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )
endif()

# --- audio_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(audio_demo apps/audio_demo/main.mm)

    target_include_directories(audio_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(audio_demo PRIVATE
        engine_core
        engine_scene
        engine_audio
        glm::glm
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
        "-framework AudioToolbox"
        "-framework CoreAudio"
    )
    target_compile_definitions(audio_demo PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )
endif()

# --- scene_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(scene_demo apps/scene_demo/main.mm)

    target_include_directories(scene_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(scene_demo PRIVATE
        engine_assets
        engine_rendering
        engine_input_glfw
        glm::glm
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(scene_demo PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )
endif()

# --- animation_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(animation_demo apps/animation_demo/main.mm)

    target_include_directories(animation_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(animation_demo PRIVATE
        engine_core
        engine_assets
        engine_scene
        engine_animation
        glm::glm
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(animation_demo PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )

    # Copy BrainStem GLB next to the binary so StdFileSystem(".")/load() finds it.
    add_custom_command(TARGET animation_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/BrainStem.glb"
            "$<TARGET_FILE_DIR:animation_demo>/BrainStem.glb"
        COMMENT "Copying BrainStem.glb to build directory"
    )
endif()

# --- ik_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(ik_demo apps/ik_demo/main.mm)

    target_include_directories(ik_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(ik_demo PRIVATE
        engine_core
        engine_assets
        engine_scene
        engine_animation
        glm::glm
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(ik_demo PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )

    # Copy BrainStem GLB next to the binary so StdFileSystem(".")/load() finds it.
    add_custom_command(TARGET ik_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/BrainStem.glb"
            "$<TARGET_FILE_DIR:ik_demo>/BrainStem.glb"
        COMMENT "Copying BrainStem.glb to ik_demo build directory"
    )
endif()

# --- ik_hand_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(ik_hand_demo apps/ik_hand_demo/main.mm)

    target_include_directories(ik_hand_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(ik_hand_demo PRIVATE
        engine_core
        engine_assets
        engine_scene
        engine_animation
        glm::glm
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(ik_hand_demo PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )

    # Copy BrainStem GLB next to the binary so StdFileSystem(".")/load() finds it.
    add_custom_command(TARGET ik_hand_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/BrainStem.glb"
            "$<TARGET_FILE_DIR:ik_hand_demo>/BrainStem.glb"
        COMMENT "Copying BrainStem.glb to ik_hand_demo build directory"
    )
endif()

# --- sama_editor app (macOS only) ---
# Native Cocoa window with bgfx Metal viewport.  No GLFW, no ImGui.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(sama_editor
        editor/main.mm
        editor/EditorApp.cpp
        editor/platform/cocoa/CocoaEditorWindow.mm
    )

    add_dependencies(sama_editor engine_shaders)

    target_include_directories(sama_editor PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(sama_editor PRIVATE
        engine_rendering
        engine_scene
        engine_memory
        engine_ecs
        glm::glm
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(sama_editor PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )
endif()
