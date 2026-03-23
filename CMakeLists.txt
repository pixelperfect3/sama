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

# --- engine_rendering static library ---
# Wraps bgfx init/shutdown/frame and the GLFW window backend.
add_library(engine_rendering STATIC
    engine/rendering/Renderer.cpp
    engine/rendering/GpuFeatures.cpp
    engine/rendering/RenderSettings.cpp
    engine/platform/desktop/GlfwWindow.cpp
)

target_include_directories(engine_rendering PUBLIC ${CMAKE_SOURCE_DIR})

target_link_libraries(engine_rendering PUBLIC bgfx bx bimg glfw)

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
