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

add_custom_target(engine_shaders ALL DEPENDS ${COMPILED_SHADERS})

# --- engine_rendering static library ---
# Wraps bgfx init/shutdown/frame and the GLFW window backend.
add_library(engine_rendering STATIC
    engine/rendering/Renderer.cpp
    engine/rendering/GpuFeatures.cpp
    engine/rendering/RenderSettings.cpp
    engine/rendering/MeshBuilder.cpp
    engine/rendering/RenderResources.cpp
    engine/rendering/ShaderLoader.cpp
    engine/rendering/systems/FrustumCullSystem.cpp
    engine/rendering/systems/DrawCallBuildSystem.cpp
    engine/platform/desktop/GlfwWindow.cpp
)

add_dependencies(engine_rendering engine_shaders)

target_include_directories(engine_rendering PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries(engine_rendering PUBLIC bgfx bx bimg glfw glm::glm)

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
    engine/ecs/Registry.cpp
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
