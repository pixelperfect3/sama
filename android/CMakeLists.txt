# ---------------------------------------------------------------------------
# Android top-level CMake build file for the Sama engine.
#
# This file is NOT meant to be invoked directly — use android/build_android.sh
# or pass -DSAMA_ANDROID=ON when configuring from the project root.
#
# It exists as a reference and for potential Gradle/ndk-build integration that
# needs a separate CMakeLists.txt entry point.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

# Force Android-specific options before including the main build.
set(SAMA_ANDROID      ON  CACHE BOOL "Building for Android" FORCE)
set(SAMA_BUILD_DEMOS  OFF CACHE BOOL "" FORCE)
set(SAMA_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(SAMA_BUILD_EDITOR OFF CACHE BOOL "" FORCE)

# Include the main project CMakeLists.txt (one directory up).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/engine)
