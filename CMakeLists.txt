cmake_minimum_required(VERSION 3.20)
project(Engine CXX)

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

FetchContent_MakeAvailable(Catch2 glm glfw bgfx_cmake)

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

# --- engine_ecs static library ---
add_library(engine_ecs STATIC engine/ecs/Registry.cpp)
target_include_directories(engine_ecs PUBLIC ${CMAKE_SOURCE_DIR})

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
    engine/rendering/LightClusterBuilder.cpp
    engine/platform/desktop/GlfwWindow.cpp
)

add_dependencies(engine_rendering engine_shaders)

target_include_directories(engine_rendering PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries(engine_rendering PUBLIC bgfx bx bimg glfw glm::glm engine_ecs)

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
    engine/threading/ThreadPool.cpp
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
)

target_link_libraries(engine_tests PRIVATE engine engine_rendering glm::glm Catch2::Catch2WithMain)

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

# --- stb (header-only, for PNG read/write in screenshot tests) ---
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
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
)

target_link_libraries(engine_screenshot_tests PRIVATE
    engine
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
    engine/input/InputState.cpp
    engine/input/InputSystem.cpp
)

target_include_directories(engine_input PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(engine_input PUBLIC engine)

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

# --- scene_demo app (macOS only) ---
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(scene_demo apps/scene_demo/main.mm)

    target_include_directories(scene_demo PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(scene_demo PRIVATE
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
