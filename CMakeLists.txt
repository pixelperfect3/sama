cmake_minimum_required(VERSION 3.20)
project(Engine C CXX)

# Compatibility for third-party deps that still declare cmake_minimum_required
# with very old versions. CMake 4.x rejects these without this override.
if(NOT CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Build options -----------------------------------------------------------
# When Sama is consumed as a FetchContent/subdirectory dependency, games only
# need the engine libraries — not demos, tests, or the editor. Default ON when
# Sama is the top-level project; OFF when consumed as a subdirectory.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_SAMA_IS_TOP_LEVEL ON)
else()
    set(_SAMA_IS_TOP_LEVEL OFF)
endif()

option(SAMA_ANDROID      "Build for Android (NDK cross-compilation)" OFF)

# When building for Android, force off desktop-only targets.
if(SAMA_ANDROID)
    set(_SAMA_IS_TOP_LEVEL OFF)
    if(NOT CMAKE_ANDROID_NDK)
        message(FATAL_ERROR "CMAKE_ANDROID_NDK not set")
    endif()
endif()

option(SAMA_BUILD_DEMOS  "Build demo applications"        ${_SAMA_IS_TOP_LEVEL})
option(SAMA_BUILD_TESTS  "Build unit + screenshot tests"  ${_SAMA_IS_TOP_LEVEL})
option(SAMA_BUILD_EDITOR "Build the Sama editor"          ${_SAMA_IS_TOP_LEVEL})

# Root of the Sama source tree — resolves correctly whether Sama is the
# top-level project or included as a subdirectory (FetchContent).
set(SAMA_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Engine interface library (header-only core)
add_library(engine INTERFACE)
target_include_directories(engine INTERFACE ${SAMA_ROOT_DIR})

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

if(NOT SAMA_ANDROID)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.3.9
    )
endif()

# bgfx.cmake wraps bgfx/bx/bimg with proper CMake targets.
# We disable all backends except Metal (macOS) and Vulkan.
if(SAMA_ANDROID)
    set(BGFX_BUILD_TOOLS    OFF CACHE BOOL "" FORCE) # no shaderc on Android
else()
    set(BGFX_BUILD_TOOLS    ON  CACHE BOOL "" FORCE) # build shaderc on desktop
endif()
set(BGFX_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL          OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS   OFF CACHE BOOL "" FORCE)

# Disable unused backends to keep the binary lean
set(BGFX_USE_DEBUG_SUFFIX OFF CACHE BOOL "" FORCE)

# GLFW: disable unneeded sub-targets (desktop only)
if(NOT SAMA_ANDROID)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
endif()

# Android emulator compat patch: bgfx unconditionally chains
# VK_KHR_fragment_shading_rate into vkGetPhysicalDeviceProperties2 pNext,
# which crashes the emulator's gfxstream Vulkan translation layer.
# The patch gates this on the extension being reported as supported.
FetchContent_Declare(
    bgfx_cmake
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG        98d6c112a427bf218b9c9aaee0128af3b0336584
    PATCH_COMMAND  ${CMAKE_COMMAND} -E env git apply --whitespace=nowarn
                   ${CMAKE_CURRENT_SOURCE_DIR}/patches/bgfx_emulator_compat.patch
                   || ${CMAKE_COMMAND} -E echo "Patch already applied or not needed"
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

if(SAMA_ANDROID)
    FetchContent_MakeAvailable(Catch2 glm bgfx_cmake cgltf JoltPhysics)
else()
    FetchContent_MakeAvailable(Catch2 glm glfw bgfx_cmake cgltf JoltPhysics)
endif()

# Suppress warnings from third-party dependencies — we don't own this code.
# All compiled third-party targets go through FetchContent and land here.
function(suppress_third_party_warnings target)
    if(TARGET ${target})
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C,CXX>:-w>
        )
        get_target_property(_target_type ${target} TYPE)
        if(_target_type STREQUAL "EXECUTABLE" AND APPLE)
            target_link_options(${target} PRIVATE "LINKER:-no_warn_duplicate_libraries")
        endif()
    endif()
endfunction()

foreach(_tp_target
    bgfx bimg bx shaderc shaderc_shared spirv-cross spirv-tools spirv-tools-opt
    glslang SPIRV MachineIndependent GenericCodeGen OSDependent OGLCompiler glslang-default-resource-limits
    glsl-optimizer glcpp mesa fcpp astc astc-codec etc2 iqa nvtt squish pvrtc edtaa3
    Jolt glfw example-common
)
    suppress_third_party_warnings(${_tp_target})
endforeach()

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

# astc-encoder — ARM's ASTC texture compression library (Apache 2.0).
# Used by engine_asset_tool to compress textures into ASTC format for Android.
# Only built on desktop (not cross-compiled for Android).
if(NOT SAMA_ANDROID)
    FetchContent_Declare(
        astcenc
        GIT_REPOSITORY https://github.com/ARM-software/astc-encoder.git
        GIT_TAG        4.8.0
    )
    # Only build the static library, not the CLI tool or tests
    set(ASTCENC_CLI OFF CACHE BOOL "" FORCE)
    set(ASTCENC_UNITTEST OFF CACHE BOOL "" FORCE)
    set(ASTCENC_SHAREDLIB OFF CACHE BOOL "" FORCE)
    set(ASTCENC_ISA_NATIVE ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(astcenc)
    # Hide symbols to avoid collisions with bgfx's bundled astc-codec
    if(TARGET astcenc-native-static)
        set_target_properties(astcenc-native-static PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            C_VISIBILITY_PRESET hidden
        )
    endif()
endif()

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

# Shader compilation uses shaderc which is desktop-only.
# On Android, pre-compiled shaders are included via the asset pipeline.
if(NOT SAMA_ANDROID)

set(SHADER_SRC_DIR     ${SAMA_ROOT_DIR}/engine/shaders)
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

    # Default empty PROFILE to 100_es for ANDROID (ESSL).
    set(_PROFILE "${PROFILE}")
    if("${_PROFILE}" STREQUAL "" AND "${PLATFORM}" STREQUAL "ANDROID")
        set(_PROFILE "100_es")
    endif()

    set(_PARSE_ARGS
        FILE       ${FILE}
        OUTPUT     ${OUT_FILE}
        BIN2C      ${BIN2C_NAME}
        ${PLATFORM}
        ${TYPE}
        VARYINGDEF ${VARYING_DEF}
        INCLUDES   ${SHADER_INCLUDE_DIR}
    )
    if(NOT "${_PROFILE}" STREQUAL "")
        list(APPEND _PARSE_ARGS PROFILE ${_PROFILE})
    endif()

    _bgfx_shaderc_parse(CLI ${_PARSE_ARGS})

    add_custom_command(
        OUTPUT  ${OUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
        COMMAND bgfx::shaderc ${CLI}
        MAIN_DEPENDENCY ${FILE}
        DEPENDS ${VARYING_DEF} bgfx::shaderc
        COMMENT "Compiling shader ${BIN2C_NAME}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set(${OUT_LIST} ${${OUT_LIST}} ${OUT_FILE} PARENT_SCOPE)
endfunction()

# Variant that uses the post-process varying def (varying_pp.def.sc).
# Identical to compile_shader() but references VARYING_PP_DEF.
function(compile_shader_pp FILE TYPE PLATFORM PROFILE BIN2C_NAME OUT_LIST)
    set(OUT_FILE ${SHADER_OUT_DIR}/${BIN2C_NAME}.bin.h)

    set(_PROFILE "${PROFILE}")
    if("${_PROFILE}" STREQUAL "" AND "${PLATFORM}" STREQUAL "ANDROID")
        set(_PROFILE "100_es")
    endif()

    set(_PARSE_ARGS
        FILE       ${FILE}
        OUTPUT     ${OUT_FILE}
        BIN2C      ${BIN2C_NAME}
        ${PLATFORM}
        ${TYPE}
        VARYINGDEF ${VARYING_PP_DEF}
        INCLUDES   ${SHADER_INCLUDE_DIR}
    )
    if(NOT "${_PROFILE}" STREQUAL "")
        list(APPEND _PARSE_ARGS PROFILE ${_PROFILE})
    endif()

    _bgfx_shaderc_parse(CLI ${_PARSE_ARGS})

    add_custom_command(
        OUTPUT  ${OUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
        COMMAND bgfx::shaderc ${CLI}
        MAIN_DEPENDENCY ${FILE}
        DEPENDS ${VARYING_PP_DEF} bgfx::shaderc
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

# Gizmo — Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_gizmo.sc VERTEX   OSX   metal  vs_gizmo_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_gizmo.sc FRAGMENT OSX   metal  fs_gizmo_mtl  COMPILED_SHADERS)

# Gizmo — SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/vs_gizmo.sc VERTEX   LINUX spirv  vs_gizmo_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_gizmo.sc FRAGMENT LINUX spirv  fs_gizmo_spv  COMPILED_SHADERS)

# Gizmo — GLSL (OpenGL)
compile_shader(
    ${SHADER_SRC_DIR}/vs_gizmo.sc VERTEX   LINUX 120    vs_gizmo_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_gizmo.sc FRAGMENT LINUX 120    fs_gizmo_glsl COMPILED_SHADERS)

# Gizmo — ESSL (OpenGLES / Android)
compile_shader(
    ${SHADER_SRC_DIR}/vs_gizmo.sc VERTEX   ANDROID ""   vs_gizmo_essl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_gizmo.sc FRAGMENT ANDROID ""   fs_gizmo_essl COMPILED_SHADERS)

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

# MSDF text — fragment shader only; it reuses vs_sprite for the vertex stream.
# Metal (macOS / iOS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_msdf.sc FRAGMENT OSX     metal  fs_msdf_mtl  COMPILED_SHADERS)
# SPIRV (Vulkan)
compile_shader(
    ${SHADER_SRC_DIR}/fs_msdf.sc FRAGMENT LINUX   spirv  fs_msdf_spv  COMPILED_SHADERS)
# GLSL (OpenGL; required by BGFX_EMBEDDED_SHADER on macOS/Linux)
compile_shader(
    ${SHADER_SRC_DIR}/fs_msdf.sc FRAGMENT LINUX   120    fs_msdf_glsl COMPILED_SHADERS)
# ESSL (OpenGLES / Android; required by BGFX_EMBEDDED_SHADER on macOS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_msdf.sc FRAGMENT ANDROID ""     fs_msdf_essl COMPILED_SHADERS)

# Slug — GPU Bezier-curve glyph rendering. Same vertex layout / varyings as
# sprite, so it shares varying.def.sc. Four platform variants like sprite.
compile_shader(
    ${SHADER_SRC_DIR}/vs_slug.sc VERTEX   OSX     metal  vs_slug_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_slug.sc FRAGMENT OSX     metal  fs_slug_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_slug.sc VERTEX   LINUX   spirv  vs_slug_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_slug.sc FRAGMENT LINUX   spirv  fs_slug_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_slug.sc VERTEX   LINUX   120    vs_slug_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_slug.sc FRAGMENT LINUX   120    fs_slug_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_slug.sc VERTEX   ANDROID ""     vs_slug_essl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_slug.sc FRAGMENT ANDROID ""     fs_slug_essl COMPILED_SHADERS)

# Skybox — unit cube + cubemap fragment shader, four platform variants.
compile_shader(
    ${SHADER_SRC_DIR}/vs_skybox.sc VERTEX   OSX     metal  vs_skybox_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_skybox.sc FRAGMENT OSX     metal  fs_skybox_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_skybox.sc VERTEX   LINUX   spirv  vs_skybox_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_skybox.sc FRAGMENT LINUX   spirv  fs_skybox_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_skybox.sc VERTEX   LINUX   120    vs_skybox_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_skybox.sc FRAGMENT LINUX   120    fs_skybox_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_skybox.sc VERTEX   ANDROID ""     vs_skybox_essl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_skybox.sc FRAGMENT ANDROID ""     fs_skybox_essl COMPILED_SHADERS)

# Rounded rect — UI shader with per-pixel rounded-box SDF for cornerRadius > 0.
compile_shader(
    ${SHADER_SRC_DIR}/vs_rounded_rect.sc VERTEX   OSX     metal  vs_rounded_rect_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_rounded_rect.sc FRAGMENT OSX     metal  fs_rounded_rect_mtl  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_rounded_rect.sc VERTEX   LINUX   spirv  vs_rounded_rect_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_rounded_rect.sc FRAGMENT LINUX   spirv  fs_rounded_rect_spv  COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_rounded_rect.sc VERTEX   LINUX   120    vs_rounded_rect_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_rounded_rect.sc FRAGMENT LINUX   120    fs_rounded_rect_glsl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/vs_rounded_rect.sc VERTEX   ANDROID ""     vs_rounded_rect_essl COMPILED_SHADERS)
compile_shader(
    ${SHADER_SRC_DIR}/fs_rounded_rect.sc FRAGMENT ANDROID ""     fs_rounded_rect_essl COMPILED_SHADERS)

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

endif()  # NOT SAMA_ANDROID (shader compilation)

# --- engine_io static library ---
add_library(engine_io STATIC engine/io/Json.cpp)
target_include_directories(engine_io PUBLIC ${SAMA_ROOT_DIR} ${rapidjson_SOURCE_DIR}/include)
target_link_libraries(engine_io PUBLIC engine glm::glm)
# rapidjson v1.1.0 emits warnings under modern C++20 compilers — suppress them.
target_compile_options(engine_io PRIVATE -Wno-deprecated-declarations)

# --- engine_memory static library ---
add_library(engine_memory STATIC engine/memory/FrameArena.cpp)
target_include_directories(engine_memory PUBLIC ${SAMA_ROOT_DIR})

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
    engine/animation/AnimStateMachine.cpp
    engine/animation/AnimStateMachineSystem.cpp
    engine/animation/AnimationSerializer.cpp
)
target_include_directories(engine_animation PUBLIC ${SAMA_ROOT_DIR})
target_link_libraries(engine_animation PUBLIC engine_ecs engine_io glm::glm)
target_compile_definitions(engine_animation PUBLIC
    ENGINE_IK_ENABLE_CCD=$<BOOL:${ENGINE_IK_ENABLE_CCD}>
    ENGINE_IK_ENABLE_FABRIK=$<BOOL:${ENGINE_IK_ENABLE_FABRIK}>
)

# --- engine_threading static library ---
add_library(engine_threading STATIC engine/threading/ThreadPool.cpp)
target_include_directories(engine_threading PUBLIC ${SAMA_ROOT_DIR})

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
    ${SAMA_ROOT_DIR}
    # <dear-imgui/imgui.h> and <iconfontheaders/...> used by bgfx's imgui.h wrapper.
    ${bgfx_cmake_SOURCE_DIR}/bgfx/3rdparty
    # imgui.h (bgfx wrapper) declares imguiCreate/BeginFrame/EndFrame — expose to callers.
    ${_BGFX_IMGUI_DIR}
)

target_link_libraries(engine_debug PUBLIC bgfx bx bimg)

# --- engine_assets static library ---
add_library(engine_assets STATIC
    engine/assets/AssetManager.cpp
    engine/assets/StdFileSystem.cpp
    engine/assets/TextureLoader.cpp
    engine/assets/GltfLoader.cpp
    engine/assets/GltfSceneSpawner.cpp
    engine/assets/ObjLoader.cpp
    engine/assets/CompressedTextureLoader.cpp
    engine/assets/EnvironmentAssetSerializer.cpp
    engine/assets/HdrLoader.cpp
    engine/assets/CubemapLoader.cpp
    engine/assets/TierAssetResolver.cpp
)

target_include_directories(engine_assets PUBLIC ${SAMA_ROOT_DIR})
target_include_directories(engine_assets PRIVATE
    ${stb_SOURCE_DIR}
    ${cgltf_SOURCE_DIR}
    ${SAMA_ROOT_DIR}/third_party/tinyobjloader
)
target_link_libraries(engine_assets PUBLIC engine_threading engine_scene engine_animation)

# --- engine_ecs static library ---
add_library(engine_ecs STATIC engine/ecs/Registry.cpp)
target_include_directories(engine_ecs PUBLIC ${SAMA_ROOT_DIR} ${SAMA_ROOT_DIR}/third_party)

# --- engine_scene static library ---
add_library(engine_scene STATIC
    engine/scene/SceneGraph.cpp
    engine/scene/TransformSystem.cpp
    engine/scene/SceneSerializer.cpp
    engine/scene/SceneManager.cpp
)
target_include_directories(engine_scene PUBLIC ${SAMA_ROOT_DIR})
target_link_libraries(engine_scene PUBLIC engine_rendering)

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
    ${SAMA_ROOT_DIR}/third_party/soloud_patches/soloud_miniaudio.cpp
)

target_compile_definitions(engine_audio PRIVATE WITH_MINIAUDIO)
target_include_directories(engine_audio PUBLIC ${SAMA_ROOT_DIR})
target_include_directories(engine_audio PUBLIC ${SOLOUD_INC_DIR})
# Use our patched miniaudio.h (v0.11) instead of SoLoud's bundled v0.9.
target_include_directories(engine_audio BEFORE PRIVATE ${SAMA_ROOT_DIR}/third_party/soloud_patches)
target_link_libraries(engine_audio PUBLIC engine_scene)

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
target_include_directories(engine_physics PUBLIC ${SAMA_ROOT_DIR})
target_link_libraries(engine_physics PUBLIC engine_scene Jolt)

# engine_physics needs the same GLM flags as the rest of the engine.
target_compile_definitions(engine_physics PUBLIC
    GLM_FORCE_INTRINSICS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_XYZW_ONLY
    GLM_ENABLE_EXPERIMENTAL
)

# --- engine_rendering static library ---
# Wraps bgfx init/shutdown/frame and the platform window backend.
set(_ENGINE_RENDERING_SOURCES
    engine/rendering/Renderer.cpp
    engine/rendering/RenderPass.cpp
    engine/rendering/GpuFeatures.cpp
    engine/rendering/RenderSettings.cpp
    engine/rendering/MeshBuilder.cpp
    engine/rendering/RenderResources.cpp
    engine/rendering/ShaderLoader.cpp
    engine/rendering/ShaderUniforms.cpp
    engine/rendering/IblResources.cpp
    engine/rendering/SkyboxRenderer.cpp
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
)

if(NOT SAMA_ANDROID)
    list(APPEND _ENGINE_RENDERING_SOURCES engine/platform/desktop/GlfwWindow.cpp)
endif()

add_library(engine_rendering STATIC ${_ENGINE_RENDERING_SOURCES})

if(NOT SAMA_ANDROID)
    add_dependencies(engine_rendering engine_shaders)
endif()

target_include_directories(engine_rendering PUBLIC
    ${SAMA_ROOT_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

if(SAMA_ANDROID)
    target_link_libraries(engine_rendering PUBLIC bgfx bimg engine_ecs engine_io)
else()
    target_link_libraries(engine_rendering PUBLIC bgfx bimg glfw engine_ecs engine_io)
endif()

# Increase bgfx bone matrix limit from default 32 to 128 for skeletal animation.
# u_model[128] = 512 vec4 uniforms, fits in BGFX_CONFIG_MAX_UNIFORMS (512).
target_compile_definitions(bgfx PUBLIC BGFX_CONFIG_MAX_BONES=128)
# Disable WebGPU/WGSL support — we don't compile WGSL shaders.
target_compile_definitions(bgfx PUBLIC BGFX_PLATFORM_SUPPORTS_WGSL=0)
if(SAMA_ANDROID)
    target_compile_definitions(bx PUBLIC BX_ANDROID_LOG_TAG="bgfx")
endif()

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

# --- engine_asset_tool static library (desktop only) ---
if(NOT ANDROID)
    add_library(engine_asset_tool STATIC
        tools/asset_tool/AssetProcessor.cpp
        tools/asset_tool/TextureProcessor.cpp
        tools/asset_tool/ShaderProcessor.cpp
        tools/asset_tool/AstcEncoderStub.cpp
    )
    target_include_directories(engine_asset_tool PUBLIC ${SAMA_ROOT_DIR})
    target_include_directories(engine_asset_tool PRIVATE ${stb_SOURCE_DIR})
    target_link_libraries(engine_asset_tool PUBLIC engine_io)

    # ASTC encoding object library — compiled separately so it can link
    # astcenc without pulling those symbols into engine_tests (which already
    # has bgfx's bundled astc-codec with colliding symbols).
    if(TARGET astcenc-native-static)
        add_library(engine_astcenc_bridge OBJECT
            tools/asset_tool/AstcEncoder.cpp
        )
        target_include_directories(engine_astcenc_bridge PRIVATE
            ${SAMA_ROOT_DIR}
            ${astcenc_SOURCE_DIR}/Source
        )
        target_link_libraries(engine_astcenc_bridge PRIVATE astcenc-native-static)
        target_compile_definitions(engine_astcenc_bridge PRIVATE SAMA_HAS_ASTCENC=1)
    endif()
endif()

if(SAMA_BUILD_TESTS)

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
    tests/assets/TestHdrLoader.cpp
    tests/assets/TestCubemapLoader.cpp
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
    tests/animation/TestAnimationSystem.cpp
    tests/animation/TestAnimStateMachine.cpp
    tests/animation/TestAnimationEvents.cpp
    tests/animation/TestAnimationSerializer.cpp
    tests/game/TestProjectConfig.cpp
    tests/game/TestTierConfig.cpp
    tests/ui/TestUiNode.cpp
    tests/ui/TestUiLayout.cpp
    tests/ui/TestBitmapFont.cpp
    tests/ui/TestMsdfFont.cpp
    tests/ui/TestSlugFont.cpp
    tests/tools/TestAssetProcessor.cpp
    tests/platform/TestAndroidKeyMap.cpp
    tests/platform/TestAndroidInput.cpp
    tests/platform/TestVirtualJoystick.cpp
    engine/platform/android/AndroidKeyMap.cpp
    engine/platform/android/VirtualJoystick.cpp
)

target_link_libraries(engine_tests PRIVATE engine_assets engine_physics engine_audio engine_game engine_ui engine_asset_tool Catch2::Catch2WithMain)

target_include_directories(engine_tests PRIVATE ${stb_SOURCE_DIR})

target_compile_definitions(engine_tests PRIVATE
    ENGINE_SOURCE_DIR="${SAMA_ROOT_DIR}"
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
set(HELMET_GLB "${SAMA_ROOT_DIR}/assets/DamagedHelmet.glb")
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
    tests/screenshot/TestSsUiPanel.cpp
    tests/screenshot/TestSsUiText.cpp
    tests/screenshot/TestSsUiWidgets.cpp
    tests/screenshot/TestSsHelmet.cpp
    tests/screenshot/TestSsIk.cpp
)

target_link_libraries(engine_screenshot_tests PRIVATE
    engine_assets
    engine_ui
    Catch2::Catch2   # NOT Catch2WithMain — we supply our own main
)

target_include_directories(engine_screenshot_tests PRIVATE
    ${stb_SOURCE_DIR}
    ${SAMA_ROOT_DIR}/tests/screenshot
)

target_compile_definitions(engine_screenshot_tests PRIVATE
    GLM_FORCE_INTRINSICS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_XYZW_ONLY
    GLM_ENABLE_EXPERIMENTAL
    ENGINE_SOURCE_DIR="${SAMA_ROOT_DIR}"
)

if(APPLE)
    target_link_libraries(engine_screenshot_tests PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
endif()

endif()  # SAMA_BUILD_TESTS (engine_tests + engine_screenshot_tests)

# --- engine_input static library ---
add_library(engine_input STATIC
    engine/input/ActionMap.cpp
    engine/input/ActionMapJson.cpp
    engine/input/InputState.cpp
    engine/input/InputSystem.cpp
)

target_include_directories(engine_input PUBLIC ${SAMA_ROOT_DIR})
target_link_libraries(engine_input PUBLIC engine_io)

# Desktop GLFW backend (not available on Android)
if(NOT SAMA_ANDROID)
    add_library(engine_input_glfw STATIC
        engine/input/desktop/GlfwInputBackend.cpp
    )
    target_include_directories(engine_input_glfw PUBLIC ${SAMA_ROOT_DIR})
    target_link_libraries(engine_input_glfw PUBLIC engine_input glfw)
endif()

# iOS backend — .mm requires Objective-C++ compilation; only built on iOS.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_library(engine_input_ios STATIC
        engine/input/ios/IosInputBackend.mm
    )
    target_include_directories(engine_input_ios PUBLIC ${SAMA_ROOT_DIR})
    target_link_libraries(engine_input_ios PUBLIC engine_input)
    target_link_libraries(engine_input_ios PUBLIC "-framework UIKit")
endif()

# Android backend — only built when targeting Android.
if(ANDROID)
    add_library(engine_input_android STATIC
        engine/input/android/AndroidInputBackend.cpp
    )
    target_include_directories(engine_input_android PUBLIC ${SAMA_ROOT_DIR})
    target_link_libraries(engine_input_android PUBLIC engine_input android)
endif()

if(SAMA_BUILD_TESTS)

# --- engine_input_tests executable ---
add_executable(engine_input_tests
    tests/input/TestActionMap.cpp
    tests/input/TestActionMapJson.cpp
    tests/input/TestInputState.cpp
    tests/input/TestInputSystemTransitions.cpp
    tests/input/TestAxisResolution.cpp
    tests/input/TestMouseDelta.cpp
    tests/input/TestTouchInput.cpp
    tests/input/TestGyroInput.cpp
    tests/input/integration/TestGlfwBackend.cpp
)

target_include_directories(engine_input_tests PRIVATE ${SAMA_ROOT_DIR})
target_link_libraries(engine_input_tests PRIVATE
    engine_input_glfw
    Catch2::Catch2WithMain
)

target_compile_definitions(engine_input_tests PRIVATE
    ENGINE_SOURCE_DIR="${SAMA_ROOT_DIR}"
)

endif()  # SAMA_BUILD_TESTS

# --- engine_core static library ---
# Centralised Engine class that owns window, renderer, default textures, shaders,
# ImGui, input, shadow renderer, and frame arena.  Desktop only — depends on
# GLFW for windowing and input.
add_library(engine_core STATIC engine/core/Engine.cpp)
target_include_directories(engine_core PUBLIC ${SAMA_ROOT_DIR})
if(NOT SAMA_ANDROID)
    target_link_libraries(engine_core PUBLIC
        engine_input_glfw
        engine_memory
        engine_debug
        engine_scene
    )
else()
    target_link_libraries(engine_core PUBLIC
        engine_input_android
        engine_memory
        engine_debug
        engine_scene
        native_app_glue
    )
endif()

# --- engine_game static library ---
# Game layer: IGame interface, GameRunner (frame loop with fixed timestep).
# Works on both desktop (GLFW) and Android (ANativeWindow).
add_library(engine_game STATIC
    engine/game/GameRunner.cpp
    engine/game/ProjectConfig.cpp
)
target_include_directories(engine_game PUBLIC ${SAMA_ROOT_DIR})
target_link_libraries(engine_game PUBLIC engine_core)

# --- engine_ui static library ---
# Retained-mode UI system: UiNode tree, UiCanvas, UiDrawList, widget types.
add_library(engine_ui STATIC
    engine/ui/UiNode.cpp
    engine/ui/UiDrawList.cpp
    engine/ui/UiCanvas.cpp
    engine/ui/UiRenderer.cpp
    engine/ui/BitmapFont.cpp
    engine/ui/DefaultFont.cpp
    engine/ui/Measure.cpp
    engine/ui/MsdfFont.cpp
    engine/ui/SlugFont.cpp
    engine/ui/widgets/UiPanel.cpp
    engine/ui/widgets/UiImage.cpp
    engine/ui/widgets/UiText.cpp
    engine/ui/widgets/UiButton.cpp
    engine/ui/widgets/UiSlider.cpp
    engine/ui/widgets/UiProgressBar.cpp
)
target_include_directories(engine_ui PUBLIC ${SAMA_ROOT_DIR} ${SAMA_ROOT_DIR}/third_party)
# stb_image is compiled privately inside BitmapFont.cpp and MsdfFont.cpp with
# STB_IMAGE_STATIC so the symbols don't collide with engine_assets/TextureLoader.cpp.
target_include_directories(engine_ui PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(engine_ui PUBLIC engine_rendering)

# FreeType2 — TTF parser used by SlugFont (and, in parallel, MsdfFont).
# We use find_package so Homebrew / system installs are picked up with no
# network fetch required. When FreeType is not found the Slug backend
# compiles as a stub (loadFromFile returns false, tests skip).
find_package(Freetype QUIET)
if(Freetype_FOUND)
    target_link_libraries(engine_ui PUBLIC Freetype::Freetype)
    target_compile_definitions(engine_ui PUBLIC SAMA_HAS_FREETYPE=1)
else()
    target_compile_definitions(engine_ui PUBLIC SAMA_HAS_FREETYPE=0)
    message(STATUS "Freetype not found — SlugFont will compile as a stub")
endif()

# ===========================================================================
# Umbrella targets — convenience aggregates for game developers.
#
# Link ONE of these from your game's CMakeLists.txt instead of listing every
# engine library manually. These are INTERFACE libraries: no extra compilation,
# just a curated dependency list.
#
# sama_minimal   — rendering + scene + ECS + game loop. No physics/audio/anim.
#                  Good for: simple 3D scenes, UI apps, prototypes.
# sama_3d        — minimal + physics + audio + animation + IK + asset loading.
#                  Good for: most 3D games (recommended default).
# sama           — everything, including input glfw backend and IO. Kitchen sink.
# sama_2d        — minimal + 2D sprite rendering only, no 3D physics.
#                  Good for: 2D games, UI tools.
# ===========================================================================

# Desktop umbrella targets — depend on engine_core (GLFW).
if(NOT SAMA_ANDROID)

# Core libs every game needs (rendering, ECS, scene, game loop, memory).
add_library(sama_minimal INTERFACE)
target_link_libraries(sama_minimal INTERFACE
    engine_core
    engine_game
    engine_rendering
    engine_scene
    engine_ecs
    engine_memory
)

# 3D games: minimal + physics + audio + animation/IK + asset loading.
add_library(sama_3d INTERFACE)
target_link_libraries(sama_3d INTERFACE
    sama_minimal
    engine_physics
    engine_audio
    engine_animation
    engine_assets
)

# Full engine: everything including input + IO + threading.
add_library(sama INTERFACE)
target_link_libraries(sama INTERFACE
    sama_3d
    engine_input
    engine_io
    engine_threading
)

# 2D games: minimal only (sprite rendering is in engine_rendering already).
add_library(sama_2d INTERFACE)
target_link_libraries(sama_2d INTERFACE
    sama_minimal
    engine_io
)

endif()  # NOT SAMA_ANDROID

# ---------------------------------------------------------------------------
# Android targets — NativeActivity entry point + shared library for APK.
# ---------------------------------------------------------------------------
if(SAMA_ANDROID)
    # android_native_app_glue — shipped with the NDK.
    set(_APP_GLUE_DIR "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")

    add_library(native_app_glue STATIC "${_APP_GLUE_DIR}/android_native_app_glue.c")
    target_include_directories(native_app_glue PUBLIC "${_APP_GLUE_DIR}")
    # ANativeActivity_onCreate must not be stripped — export it.
    target_link_libraries(native_app_glue PUBLIC android log)

    # bgfx's example-common compiles entry_android.cpp which needs the
    # native app glue header. Add the include path so it compiles.
    if(TARGET example-common)
        target_include_directories(example-common PRIVATE "${_APP_GLUE_DIR}")
    endif()

    # engine_android — platform layer for Android.
    add_library(engine_android STATIC
        engine/platform/android/AndroidApp.cpp
        engine/platform/android/AndroidFileSystem.cpp
        engine/platform/android/AndroidGlobals.cpp
        engine/platform/android/AndroidGyro.cpp
        engine/platform/android/AndroidInput.cpp
        engine/platform/android/AndroidKeyMap.cpp
        engine/platform/android/AndroidWindow.cpp
        engine/platform/android/VirtualJoystick.cpp
    )
    target_include_directories(engine_android PUBLIC ${SAMA_ROOT_DIR})
    target_link_libraries(engine_android PUBLIC
        native_app_glue
        engine_rendering
        engine_game
        engine_core
        engine_memory
        android
        log
    )

    # sama_android — shared library loaded by NativeActivity.
    # Games link against this and provide samaCreateGame().
    add_library(sama_android SHARED
        $<TARGET_OBJECTS:engine_android>
    )
    target_include_directories(sama_android PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(sama_android PRIVATE
        engine_android
        engine_game
        engine_core
        engine_scene
        engine_ecs
        engine_io
        engine_rendering
        engine_ui
        engine_animation
        engine_physics
        engine_audio
        engine_assets
        engine_threading
        vulkan
        EGL
        GLESv3
    )
    # Ensure ANativeActivity_onCreate is not stripped from the shared library.
    set_target_properties(sama_android PROPERTIES
        LINK_FLAGS "-u ANativeActivity_onCreate"
    )
    # On Android, link the test game into sama_android.
    target_sources(sama_android PRIVATE apps/android_test/AndroidTestGame.cpp)

endif()  # SAMA_ANDROID

# --- android_test app (desktop, for verifying the test game compiles) ---
if(NOT SAMA_ANDROID)
    add_executable(android_test apps/android_test/AndroidTestGame.cpp)
    target_include_directories(android_test PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(android_test PRIVATE
        engine_core
        engine_game
        engine_rendering
        engine_ui
        engine_scene
        engine_ecs
        engine_memory
        engine_io
        engine_threading
        engine_input_glfw
    )
endif()

# --- bake_default_env tool (desktop only) ---
# Generates the procedural sky cubemap + BRDF LUT and writes it to disk
# (default: assets/env/default.env). The editor loads this file at startup
# instead of regenerating, dropping startup from ~5 s to <50 ms. Run
# manually whenever the procedural sky model in IblResources is changed:
#
#   cmake --build build --target bake_default_env
#   ./build/bake_default_env
#   git add assets/env/default.env && git commit
#
if(NOT SAMA_ANDROID)
    add_executable(bake_default_env
        tools/bake_default_env/main.cpp
    )
    target_link_libraries(bake_default_env PRIVATE
        engine_assets
        engine_rendering
    )
endif()

if(SAMA_BUILD_DEMOS)

# --- helmet_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(helmet_demo apps/helmet_demo/main.mm)

    target_include_directories(helmet_demo PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(helmet_demo PRIVATE
        engine_core
        engine_assets
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

    target_include_directories(hierarchy_demo PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(hierarchy_demo PRIVATE
        engine_core
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

    target_include_directories(physics_demo PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(physics_demo PRIVATE
        engine_core
        engine_physics
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

# --- physics_demo_v2 app (macOS only) ---
# Same physics demo migrated to IGame/GameRunner pattern.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(physics_demo_v2
        apps/physics_demo/main_v2.mm
        apps/physics_demo/PhysicsGame.cpp
    )

    target_include_directories(physics_demo_v2 PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(physics_demo_v2 PRIVATE
        engine_game
        engine_physics
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(physics_demo_v2 PRIVATE
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

    target_include_directories(audio_demo PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(audio_demo PRIVATE
        engine_core
        engine_audio
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

    target_include_directories(scene_demo PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(scene_demo PRIVATE
        engine_assets
        engine_input_glfw
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

    target_include_directories(animation_demo PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(animation_demo PRIVATE
        engine_core
        engine_assets
        engine_animation
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

    # Copy Fox GLB next to the binary so StdFileSystem(".")/load() finds it.
    add_custom_command(TARGET animation_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SAMA_ROOT_DIR}/assets/Fox.glb"
            "$<TARGET_FILE_DIR:animation_demo>/Fox.glb"
        COMMENT "Copying Fox.glb to build directory"
    )
endif()

# --- ik_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(ik_demo apps/ik_demo/main.mm)

    target_include_directories(ik_demo PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(ik_demo PRIVATE
        engine_core
        engine_assets
        engine_animation
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
            "${SAMA_ROOT_DIR}/assets/BrainStem.glb"
            "$<TARGET_FILE_DIR:ik_demo>/BrainStem.glb"
        COMMENT "Copying BrainStem.glb to ik_demo build directory"
    )
endif()

# --- ik_hand_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(ik_hand_demo apps/ik_hand_demo/main.mm)

    target_include_directories(ik_hand_demo PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(ik_hand_demo PRIVATE
        engine_core
        engine_assets
        engine_animation
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
            "${SAMA_ROOT_DIR}/assets/BrainStem.glb"
            "$<TARGET_FILE_DIR:ik_hand_demo>/BrainStem.glb"
        COMMENT "Copying BrainStem.glb to ik_hand_demo build directory"
    )
endif()

# --- ui_test app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(ui_test
        apps/ui_test/main.mm
        apps/ui_test/UiTestApp.cpp
    )

    target_include_directories(ui_test PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(ui_test PRIVATE
        sama_minimal
        engine_ui
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
    )
    target_compile_definitions(ui_test PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )
endif()

endif()  # SAMA_BUILD_DEMOS

if(SAMA_BUILD_EDITOR)

# --- sama_editor app (macOS only) ---
# Native Cocoa window with bgfx Metal viewport.  No GLFW, no ImGui.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(sama_editor
        editor/main.mm
        editor/EditorApp.cpp
        editor/panels/HierarchyPanel.cpp
        editor/panels/PropertiesPanel.cpp
        editor/panels/AssetBrowserPanel.cpp
        editor/panels/ConsolePanel.cpp
        editor/panels/ResourcePanel.cpp
        editor/panels/AnimationPanel.cpp
        editor/EditorLog.cpp
        editor/inspectors/TransformInspector.cpp
        editor/inspectors/MaterialInspector.cpp
        editor/inspectors/LightInspector.cpp
        editor/inspectors/NameInspector.cpp
        editor/inspectors/RigidBodyInspector.cpp
        editor/inspectors/ColliderInspector.cpp
        editor/gizmo/TransformGizmo.cpp
        editor/gizmo/GizmoRenderer.cpp
        editor/undo/CommandStack.cpp
        editor/undo/SetTransformCommand.cpp
        editor/undo/CreateEntityCommand.cpp
        editor/undo/DeleteEntityCommand.cpp
        editor/platform/cocoa/CocoaEditorWindow.mm
        editor/platform/cocoa/CocoaHierarchyView.mm
        editor/platform/cocoa/CocoaPropertiesView.mm
        editor/platform/cocoa/CocoaConsoleView.mm
        editor/platform/cocoa/CocoaResourceView.mm
        editor/platform/cocoa/CocoaAnimationView.mm
        editor/platform/cocoa/StateMachineGraphView.mm
    )

    add_dependencies(sama_editor engine_shaders)

    target_include_directories(sama_editor PRIVATE
        ${SAMA_ROOT_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(sama_editor PRIVATE
        engine_scene
        engine_memory
        engine_assets
        engine_threading
        engine_physics
        engine_ui
        engine_animation
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
        "-framework UniformTypeIdentifiers"
    )
    target_compile_definitions(sama_editor PRIVATE
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )
endif()

endif()  # SAMA_BUILD_EDITOR

# ===========================================================================
# Size test binaries (macOS only).
# Minimal executables used only to measure the binary footprint of each
# Sama umbrella target. Build with: cmake --build <dir> --target size_test_*
# ===========================================================================
if(SAMA_BUILD_DEMOS AND APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(SAMA_SIZE_TEST_FRAMEWORKS
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework Metal"
        "-framework MetalKit"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
        "-framework CoreAudio"
    )
    set(SAMA_SIZE_TEST_DEFS
        GLM_FORCE_INTRINSICS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_FORCE_XYZW_ONLY
        GLM_ENABLE_EXPERIMENTAL
    )

    add_executable(size_test_minimal apps/size_test/size_test_minimal.mm)
    target_include_directories(size_test_minimal PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(size_test_minimal PRIVATE sama_minimal ${SAMA_SIZE_TEST_FRAMEWORKS})
    target_compile_definitions(size_test_minimal PRIVATE ${SAMA_SIZE_TEST_DEFS})

    add_executable(size_test_3d apps/size_test/size_test_3d.mm)
    target_include_directories(size_test_3d PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(size_test_3d PRIVATE sama_3d ${SAMA_SIZE_TEST_FRAMEWORKS})
    target_compile_definitions(size_test_3d PRIVATE ${SAMA_SIZE_TEST_DEFS})

    add_executable(size_test_full apps/size_test/size_test_full.mm)
    target_include_directories(size_test_full PRIVATE ${SAMA_ROOT_DIR})
    target_link_libraries(size_test_full PRIVATE sama ${SAMA_SIZE_TEST_FRAMEWORKS})
    target_compile_definitions(size_test_full PRIVATE ${SAMA_SIZE_TEST_DEFS})
endif()

# --- sama_asset_tool CLI (desktop only) ---
if(NOT ANDROID)
    add_executable(sama_asset_tool tools/asset_tool/main.cpp)
    target_include_directories(sama_asset_tool PRIVATE ${SAMA_ROOT_DIR})
    if(TARGET engine_astcenc_bridge)
        # Link order matters: bridge objects reference symbols in
        # engine_asset_tool (the stub), so the static lib must come last.
        target_link_libraries(sama_asset_tool PRIVATE
            engine_astcenc_bridge engine_asset_tool astcenc-native-static)
    else()
        target_link_libraries(sama_asset_tool PRIVATE engine_asset_tool)
    endif()
endif()

# --- APK build convenience target (desktop only) ---
# Calls the Gradle-free APK build script. Requires ANDROID_NDK, ANDROID_SDK_ROOT,
# and build-tools (aapt2, zipalign, apksigner) to be installed.
if(NOT SAMA_ANDROID)
    add_custom_target(build_apk
        COMMAND ${CMAKE_SOURCE_DIR}/android/build_apk.sh --tier mid
        DEPENDS sama_asset_tool
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building Android APK (mid tier)"
    )
endif()
