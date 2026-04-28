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

# ── Configure ───────────────────────────────────────────────────────────────

echo "[1/2] Configuring (cmake -G Xcode)..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -G Xcode \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DSAMA_IOS=ON \
    -DSAMA_IOS_PLATFORM=SIMULATOR \
    -DSAMA_BUILD_TESTS=OFF \
    -DSAMA_BUILD_DEMOS=OFF \
    -DSAMA_BUILD_EDITOR=OFF

# ── Build ───────────────────────────────────────────────────────────────────

echo "[2/2] Building scheme '${TARGET}' for iphonesimulator..."
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
