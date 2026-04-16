#!/bin/bash
# ---------------------------------------------------------------------------
# Build the Sama engine for Android using the NDK toolchain.
#
# Usage: ./android/build_android.sh [arm64-v8a|armeabi-v7a] [Debug|Release]
#
# Environment variables:
#   ANDROID_NDK — path to the Android NDK (default: $HOME/Android/Sdk/ndk/26.1.10909125)
# ---------------------------------------------------------------------------

set -euo pipefail

ABI=${1:-arm64-v8a}
BUILD_TYPE=${2:-Release}
ANDROID_NDK=${ANDROID_NDK:-$HOME/Android/Sdk/ndk/26.1.10909125}

# Validate ABI
VALID_ABIS="arm64-v8a armeabi-v7a x86 x86_64"
if ! echo "${VALID_ABIS}" | grep -qw "${ABI}"; then
    echo "ERROR: Invalid ABI '${ABI}'"
    echo "Valid ABIs: ${VALID_ABIS}"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [ ! -d "${ANDROID_NDK}" ]; then
    echo "ERROR: Android NDK not found at ${ANDROID_NDK}"
    echo "Set ANDROID_NDK to point to your NDK installation."
    exit 1
fi

TOOLCHAIN_FILE="${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
if [ ! -f "${TOOLCHAIN_FILE}" ]; then
    echo "ERROR: NDK toolchain file not found at ${TOOLCHAIN_FILE}"
    exit 1
fi

echo "=== Sama Engine — Android build ==="
echo "  ABI:        ${ABI}"
echo "  Build type: ${BUILD_TYPE}"
echo "  NDK:        ${ANDROID_NDK}"
echo ""

cmake -S "${PROJECT_ROOT}" -B "${PROJECT_ROOT}/build/android/${ABI}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DSAMA_ANDROID=ON

cmake --build "${PROJECT_ROOT}/build/android/${ABI}" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
