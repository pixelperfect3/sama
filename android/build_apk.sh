#!/bin/bash
# ---------------------------------------------------------------------------
# Build an Android APK for the Sama engine (Gradle-free).
#
# Usage: ./android/build_apk.sh [options]
#   --tier <low|mid|high>     Quality tier (default: mid)
#   --abi <arm64-v8a|...>     Target ABI (default: arm64-v8a)
#   --debug                   Debug build (default: release)
#   --keystore <path>         Keystore for signing (default: debug keystore)
#   --output <path>           Output APK path (default: build/android/Game.apk)
#   --install                 Install via adb after build
#   --app-name <name>         Application name (default: "Sama Game")
#   --package <id>            Package ID (default: com.sama.game)
#
# Environment variables:
#   ANDROID_NDK      — path to the Android NDK
#   ANDROID_SDK_ROOT — path to the Android SDK (for aapt2, zipalign, apksigner)
#   ANDROID_HOME     — fallback for ANDROID_SDK_ROOT
# ---------------------------------------------------------------------------

set -euo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────────

TIER="mid"
ABI="arm64-v8a"
BUILD_TYPE="Release"
KEYSTORE=""
OUTPUT=""
INSTALL=false
APP_NAME="Sama Game"
PACKAGE_ID="com.sama.game"

# ── Parse arguments ──────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tier)      TIER="$2";       shift 2 ;;
        --abi)       ABI="$2";        shift 2 ;;
        --debug)     BUILD_TYPE="Debug"; shift ;;
        --keystore)  KEYSTORE="$2";   shift 2 ;;
        --output)    OUTPUT="$2";     shift 2 ;;
        --install)   INSTALL=true;    shift ;;
        --app-name)  APP_NAME="$2";   shift 2 ;;
        --package)   PACKAGE_ID="$2"; shift 2 ;;
        -h|--help)
            head -17 "$0" | tail -15
            exit 0
            ;;
        *)
            echo "ERROR: Unknown option '$1'"
            echo "Run with --help for usage."
            exit 1
            ;;
    esac
done

# ── Resolve paths ────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [ -z "$OUTPUT" ]; then
    OUTPUT="${PROJECT_ROOT}/build/android/Game.apk"
fi

STAGING_DIR="${PROJECT_ROOT}/build/android/apk_staging"
ANDROID_SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"

# ── Validate tier ────────────────────────────────────────────────────────────

if [[ "$TIER" != "low" && "$TIER" != "mid" && "$TIER" != "high" ]]; then
    echo "ERROR: Invalid tier '${TIER}'. Must be one of: low, mid, high"
    exit 1
fi

# ── Validate dependencies ───────────────────────────────────────────────────

ERRORS=0

if [ -z "${ANDROID_NDK:-}" ]; then
    echo "ERROR: ANDROID_NDK is not set."
    echo "  Set it to your NDK installation, e.g.:"
    echo "  export ANDROID_NDK=\$HOME/Android/Sdk/ndk/26.1.10909125"
    ERRORS=1
elif [ ! -d "$ANDROID_NDK" ]; then
    echo "ERROR: ANDROID_NDK directory does not exist: $ANDROID_NDK"
    ERRORS=1
fi

if [ -z "$ANDROID_SDK" ]; then
    echo "ERROR: Neither ANDROID_SDK_ROOT nor ANDROID_HOME is set."
    echo "  Set one of them to your Android SDK installation, e.g.:"
    echo "  export ANDROID_SDK_ROOT=\$HOME/Android/Sdk"
    ERRORS=1
elif [ ! -d "$ANDROID_SDK" ]; then
    echo "ERROR: Android SDK directory does not exist: $ANDROID_SDK"
    ERRORS=1
fi

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found in PATH."
    ERRORS=1
fi

if ! command -v keytool &>/dev/null; then
    echo "ERROR: keytool not found in PATH (required for debug keystore)."
    ERRORS=1
fi

# Locate Android SDK build-tools binaries
AAPT2=""
ZIPALIGN=""
APKSIGNER=""

if [ -n "$ANDROID_SDK" ] && [ -d "$ANDROID_SDK" ]; then
    # Find the latest installed build-tools version
    BUILD_TOOLS_DIR=""
    if [ -d "$ANDROID_SDK/build-tools" ]; then
        BUILD_TOOLS_DIR=$(ls -d "$ANDROID_SDK/build-tools"/*/ 2>/dev/null \
            | sort -V | tail -1 | sed 's:/$::')
    fi

    if [ -n "$BUILD_TOOLS_DIR" ]; then
        AAPT2="${BUILD_TOOLS_DIR}/aapt2"
        ZIPALIGN="${BUILD_TOOLS_DIR}/zipalign"
        APKSIGNER="${BUILD_TOOLS_DIR}/apksigner"
    fi
fi

# Fall back to PATH for each tool
if [ ! -x "$AAPT2" ]; then
    AAPT2=$(command -v aapt2 2>/dev/null || true)
fi
if [ ! -x "$ZIPALIGN" ]; then
    ZIPALIGN=$(command -v zipalign 2>/dev/null || true)
fi
if [ ! -x "$APKSIGNER" ]; then
    APKSIGNER=$(command -v apksigner 2>/dev/null || true)
fi

if [ -z "$AAPT2" ]; then
    echo "ERROR: aapt2 not found."
    echo "  Install Android SDK build-tools:"
    echo "  sdkmanager --install 'build-tools;34.0.0'"
    ERRORS=1
fi

if [ -z "$ZIPALIGN" ]; then
    echo "ERROR: zipalign not found."
    echo "  Install Android SDK build-tools:"
    echo "  sdkmanager --install 'build-tools;34.0.0'"
    ERRORS=1
fi

if [ -z "$APKSIGNER" ]; then
    echo "ERROR: apksigner not found."
    echo "  Install Android SDK build-tools:"
    echo "  sdkmanager --install 'build-tools;34.0.0'"
    ERRORS=1
fi

# Locate android.jar
ANDROID_JAR=""
if [ -n "$ANDROID_SDK" ] && [ -d "$ANDROID_SDK" ]; then
    # Prefer API 34, fall back to highest available
    if [ -f "$ANDROID_SDK/platforms/android-34/android.jar" ]; then
        ANDROID_JAR="$ANDROID_SDK/platforms/android-34/android.jar"
    else
        ANDROID_JAR=$(ls "$ANDROID_SDK/platforms"/android-*/android.jar 2>/dev/null \
            | sort -V | tail -1 || true)
    fi
fi

if [ -z "$ANDROID_JAR" ]; then
    echo "ERROR: android.jar not found."
    echo "  Install an Android platform:"
    echo "  sdkmanager --install 'platforms;android-34'"
    ERRORS=1
fi

if [ "$ERRORS" -ne 0 ]; then
    echo ""
    echo "Missing dependencies. Please install the above and try again."
    exit 1
fi

# ── Print configuration ─────────────────────────────────────────────────────

echo "=== Sama Engine — APK Build ==="
echo "  Tier:        ${TIER}"
echo "  ABI:         ${ABI}"
echo "  Build type:  ${BUILD_TYPE}"
echo "  Package:     ${PACKAGE_ID}"
echo "  App name:    ${APP_NAME}"
echo "  Output:      ${OUTPUT}"
echo ""

# ── Step 1: Build native library ────────────────────────────────────────────

echo "[1/7] Building native library..."
"${PROJECT_ROOT}/android/build_android.sh" "$ABI" "$BUILD_TYPE"

SO_PATH="${PROJECT_ROOT}/build/android/${ABI}/libsama_android.so"
if [ ! -f "$SO_PATH" ]; then
    echo "ERROR: Native library not found at ${SO_PATH}"
    echo "  The NDK build may have failed or produced a different output name."
    exit 1
fi

# ── Step 2: Process assets ──────────────────────────────────────────────────

echo "[2/7] Processing assets for tier '${TIER}'..."
ASSET_TOOL="${PROJECT_ROOT}/build/sama_asset_tool"
ASSETS_DIR="${PROJECT_ROOT}/assets"
ASSETS_OUTPUT="${STAGING_DIR}/assets"

if [ -d "$ASSETS_DIR" ] && [ -x "$ASSET_TOOL" ]; then
    "$ASSET_TOOL" \
        --input "$ASSETS_DIR" \
        --output "$ASSETS_OUTPUT" \
        --target android \
        --tier "$TIER"
elif [ -d "$ASSETS_DIR" ] && [ ! -x "$ASSET_TOOL" ]; then
    echo "  WARNING: sama_asset_tool not found at ${ASSET_TOOL}"
    echo "  Build it first: cmake --build build --target sama_asset_tool"
    echo "  Copying raw assets instead..."
    rm -rf "$ASSETS_OUTPUT"
    mkdir -p "$ASSETS_OUTPUT"
    cp -r "$ASSETS_DIR"/* "$ASSETS_OUTPUT"/
else
    echo "  WARNING: No assets directory found at ${ASSETS_DIR}"
    mkdir -p "$ASSETS_OUTPUT"
fi

# ── Step 3: Create APK staging directory ────────────────────────────────────

echo "[3/7] Staging APK contents..."
mkdir -p "${STAGING_DIR}/lib/${ABI}"

# Copy native library
cp "$SO_PATH" "${STAGING_DIR}/lib/${ABI}/libsama_android.so"

# Copy the shared C++ runtime (required when ANDROID_STL=c++_shared)
LIBCXX_PATH="${ANDROID_NDK}/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/lib"
case "$ABI" in
    arm64-v8a)   LIBCXX_ARCH="aarch64-linux-android" ;;
    armeabi-v7a) LIBCXX_ARCH="arm-linux-androideabi" ;;
    x86_64)      LIBCXX_ARCH="x86_64-linux-android" ;;
    x86)         LIBCXX_ARCH="i686-linux-android" ;;
esac
if [ -f "${LIBCXX_PATH}/${LIBCXX_ARCH}/libc++_shared.so" ]; then
    cp "${LIBCXX_PATH}/${LIBCXX_ARCH}/libc++_shared.so" "${STAGING_DIR}/lib/${ABI}/"
else
    echo "WARNING: libc++_shared.so not found for ${ABI}"
fi

# Generate manifest from template
sed -e "s/com\\.sama\\.engine/${PACKAGE_ID//./\\.}/g" \
    -e "s/Sama Engine/${APP_NAME}/g" \
    "${PROJECT_ROOT}/android/AndroidManifest.xml" > "${STAGING_DIR}/AndroidManifest.xml"

# ── Step 4: Compile resources and create base APK ───────────────────────────

echo "[4/7] Creating base APK..."
BASE_APK="${PROJECT_ROOT}/build/android/base.apk"

# For MVP we have no custom resources (no icons, no layouts).
# Create the APK directly from the manifest.
"$AAPT2" link -o "$BASE_APK" \
    --manifest "${STAGING_DIR}/AndroidManifest.xml" \
    -I "$ANDROID_JAR"

# ── Step 5: Add native lib + assets to APK ──────────────────────────────────

echo "[5/7] Adding native library and assets to APK..."
UNSIGNED_APK="${PROJECT_ROOT}/build/android/unsigned.apk"
cp "$BASE_APK" "$UNSIGNED_APK"

# Add lib/ and assets/ directories into the APK (which is a zip)
(cd "$STAGING_DIR" && zip -r "$UNSIGNED_APK" lib/ assets/)

# ── Step 6: Align ───────────────────────────────────────────────────────────

echo "[6/7] Aligning APK..."
ALIGNED_APK="${PROJECT_ROOT}/build/android/aligned.apk"
"$ZIPALIGN" -f 4 "$UNSIGNED_APK" "$ALIGNED_APK"

# ── Step 7: Sign ────────────────────────────────────────────────────────────

echo "[7/7] Signing APK..."
mkdir -p "$(dirname "$OUTPUT")"

if [ -n "$KEYSTORE" ]; then
    "$APKSIGNER" sign --ks "$KEYSTORE" --out "$OUTPUT" "$ALIGNED_APK"
else
    # Use or create debug keystore
    DEBUG_KS="$HOME/.android/debug.keystore"
    "${PROJECT_ROOT}/android/create_debug_keystore.sh" "$DEBUG_KS"
    "$APKSIGNER" sign \
        --ks "$DEBUG_KS" \
        --ks-pass pass:android \
        --ks-key-alias androiddebugkey \
        --key-pass pass:android \
        --out "$OUTPUT" \
        "$ALIGNED_APK"
fi

# ── Clean up intermediate files ─────────────────────────────────────────────

rm -f "$BASE_APK" "$UNSIGNED_APK" "$ALIGNED_APK"

echo ""
echo "=== APK built successfully ==="
echo "  Output: ${OUTPUT}"

# Report APK size
if [ -f "$OUTPUT" ]; then
    SIZE=$(du -h "$OUTPUT" | cut -f1)
    echo "  Size:   ${SIZE}"
fi

# ── Optional: Install via adb ───────────────────────────────────────────────

if [ "$INSTALL" = true ]; then
    echo ""
    echo "Installing APK via adb..."
    if ! command -v adb &>/dev/null; then
        echo "ERROR: adb not found in PATH."
        echo "  Add Android SDK platform-tools to your PATH."
        exit 1
    fi
    adb install -r "$OUTPUT"
    echo "APK installed successfully."
fi
