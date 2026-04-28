#!/bin/bash
# ---------------------------------------------------------------------------
# Build the Sama engine for the iOS Simulator (Xcode-based).
#
# Phase A scope: configure with cmake -G Xcode + the iOS toolchain file, then
# xcodebuild the ios_test scheme against the iphonesimulator SDK.  No code
# signing, no real-device deployment, no IPA — Phase D's job.
#
# Usage:
#   ./ios/build_ios.sh [options]
#     --configuration <Debug|Release>  Default: Debug
#     --build-dir <path>               Default: build/ios-sim
#     --target <name>                  Default: ios_test
#     --clean                          Wipe build dir before configure
#
# Output: ${BUILD_DIR}/${configuration}-iphonesimulator/${target}.app
# ---------------------------------------------------------------------------

set -euo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────────

CONFIGURATION="Debug"
TARGET="ios_test"
CLEAN=false

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/ios-sim"

# ── Parse arguments ──────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --configuration) CONFIGURATION="$2"; shift 2 ;;
        --build-dir)     BUILD_DIR="$2";     shift 2 ;;
        --target)        TARGET="$2";        shift 2 ;;
        --clean)         CLEAN=true;         shift ;;
        -h|--help)
            head -16 "$0" | tail -14
            exit 0
            ;;
        *)
            echo "ERROR: Unknown option '$1'" >&2
            exit 1
            ;;
    esac
done

# ── Validate dependencies ───────────────────────────────────────────────────

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found in PATH." >&2
    exit 1
fi
if ! command -v xcodebuild &>/dev/null; then
    echo "ERROR: xcodebuild not found. Install Xcode Command Line Tools." >&2
    exit 1
fi

TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/ios.toolchain.cmake"
if [ ! -f "${TOOLCHAIN_FILE}" ]; then
    echo "ERROR: iOS toolchain file not found at ${TOOLCHAIN_FILE}" >&2
    exit 1
fi

echo "=== Sama Engine — iOS Simulator build ==="
echo "  Configuration: ${CONFIGURATION}"
echo "  Target:        ${TARGET}"
echo "  Build dir:     ${BUILD_DIR}"
echo ""

# ── Clean ───────────────────────────────────────────────────────────────────

if [ "${CLEAN}" = true ]; then
    echo "Cleaning ${BUILD_DIR}..."
    rm -rf "${BUILD_DIR}"
fi

# ── Host shaderc ────────────────────────────────────────────────────────────
# bgfx's shaderc tool is needed to compile Metal shader bytecode into the
# generated/shaders/*_mtl.bin.h headers that engine_rendering #includes.  It
# cannot be cross-compiled (it's a host build-time tool), so we either reuse
# a shaderc built by a prior desktop configure, or do a minimal host build
# ourselves.  Either path produces a path we hand to the iOS configure via
# SAMA_HOST_SHADERC.
#
# Preferred locations (in order):
#   1. ${PROJECT_ROOT}/build/_deps/bgfx_cmake-build/cmake/bgfx/shaderc
#      — a normal desktop configure already produces this.
#   2. ${PROJECT_ROOT}/build/host-shaderc/_deps/bgfx_cmake-build/cmake/bgfx/shaderc
#      — fallback host build the script triggers when (1) is missing.

HOST_SHADERC=""
for _candidate in \
    "${PROJECT_ROOT}/build/_deps/bgfx_cmake-build/cmake/bgfx/shaderc" \
    "${PROJECT_ROOT}/build/host-shaderc/_deps/bgfx_cmake-build/cmake/bgfx/shaderc"
do
    if [ -x "${_candidate}" ]; then
        HOST_SHADERC="${_candidate}"
        break
    fi
done

if [ -z "${HOST_SHADERC}" ]; then
    echo "[1/3] Building host shaderc (one-time, ~3 min)..."
    HOST_BUILD_DIR="${PROJECT_ROOT}/build/host-shaderc"
    # ASTC encoder's CMake refuses ISA_NATIVE under universal builds; we
    # disable astcenc here since shaderc doesn't need it (texture compression
    # is sama_asset_tool's job, not shaderc's).
    cmake -S "${PROJECT_ROOT}" -B "${HOST_BUILD_DIR}" \
        -DSAMA_BUILD_TESTS=OFF \
        -DSAMA_BUILD_DEMOS=OFF \
        -DSAMA_BUILD_EDITOR=OFF \
        -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
    cmake --build "${HOST_BUILD_DIR}" --target shaderc -j"$(sysctl -n hw.ncpu)"
    HOST_SHADERC="${HOST_BUILD_DIR}/_deps/bgfx_cmake-build/cmake/bgfx/shaderc"
fi

if [ ! -x "${HOST_SHADERC}" ]; then
    echo "ERROR: host shaderc build did not produce ${HOST_SHADERC}" >&2
    exit 1
fi
echo "  Host shaderc: ${HOST_SHADERC}"

# ── Configure ───────────────────────────────────────────────────────────────

echo "[2/3] Configuring (cmake -G Xcode)..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -G Xcode \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DSAMA_IOS=ON \
    -DSAMA_IOS_PLATFORM=SIMULATOR \
    -DSAMA_HOST_SHADERC="${HOST_SHADERC}" \
    -DSAMA_BUILD_TESTS=OFF \
    -DSAMA_BUILD_DEMOS=OFF \
    -DSAMA_BUILD_EDITOR=OFF

# ── Build ───────────────────────────────────────────────────────────────────

echo "[3/3] Building scheme '${TARGET}' for iphonesimulator..."
xcodebuild \
    -project "${BUILD_DIR}/Engine.xcodeproj" \
    -scheme "${TARGET}" \
    -configuration "${CONFIGURATION}" \
    -sdk iphonesimulator \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGN_IDENTITY="" \
    build

APP_PATH="${BUILD_DIR}/${CONFIGURATION}-iphonesimulator/${TARGET}.app"
if [ -d "${APP_PATH}" ]; then
    echo ""
    echo "=== Build OK ==="
    echo "  App bundle:  ${APP_PATH}"
    echo ""
    echo "Boot a simulator and install:"
    echo "  xcrun simctl list devices | grep -i 'iPhone 15'"
    echo "  xcrun simctl boot <UDID>"
    echo "  open -a Simulator"
    echo "  xcrun simctl install booted '${APP_PATH}'"
    echo "  xcrun simctl launch booted com.sama.iostest"
    echo "  xcrun simctl io booted screenshot /tmp/ios_test.png"
else
    echo "ERROR: expected app bundle not found at ${APP_PATH}" >&2
    exit 1
fi
