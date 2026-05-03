#!/bin/bash
# ---------------------------------------------------------------------------
# Build an Android App Bundle (AAB) for Play Store distribution.
#
# Usage: ./android/build_aab.sh [options]
#   --tier <low|mid|high>       Quality tier (default: mid)
#   --keystore <path>           Keystore for signing (required for Play Store)
#   --ks-pass <password>        Keystore password (prompted if not provided).
#                               SECURITY: appears in process listings and shell
#                               history; prefer --ks-pass-env in CI.
#   --ks-pass-env <ENV_VAR>     Read keystore password from named env var.
#   --ks-alias <alias>          Key alias in keystore (default: sama)
#   --key-pass <password>       Key password (prompted if not provided).
#   --key-pass-env <ENV_VAR>    Read key password from named env var.
#   --no-clean-staging          Skip wiping build/android/aab_staging before
#                               assembly (faster local iteration; off by default
#                               since staging is also cleaned at end of run).
#   --output <path>             Output AAB path (default: build/android/Game.aab)
#   --app-name <name>           Application name (default: "Sama Game")
#   --package <id>              Package ID (default: com.sama.game)
#   --skip-armeabi              Skip armeabi-v7a build (arm64-v8a only)
#
# Environment variables:
#   ANDROID_NDK      — path to the Android NDK
#   ANDROID_SDK_ROOT — path to the Android SDK (for aapt2)
#   ANDROID_HOME     — fallback for ANDROID_SDK_ROOT
# ---------------------------------------------------------------------------

set -euo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────────

TIER="mid"
KEYSTORE=""
KS_PASS=""
KS_PASS_ENV=""
KS_ALIAS="sama"
KEY_PASS=""
KEY_PASS_ENV=""
CLEAN_STAGING=true
OUTPUT=""
APP_NAME="Sama Game"
PACKAGE_ID="com.sama.game"
SKIP_ARMEABI=false

# ── Parse arguments ──────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tier)              TIER="$2";              shift 2 ;;
        --keystore)          KEYSTORE="$2";          shift 2 ;;
        --ks-pass)           KS_PASS="$2";           shift 2 ;;
        --ks-pass-env)       KS_PASS_ENV="$2";       shift 2 ;;
        --ks-alias)          KS_ALIAS="$2";          shift 2 ;;
        --key-pass)          KEY_PASS="$2";          shift 2 ;;
        --key-pass-env)      KEY_PASS_ENV="$2";      shift 2 ;;
        --no-clean-staging)  CLEAN_STAGING=false;    shift ;;
        --output)            OUTPUT="$2";            shift 2 ;;
        --app-name)          APP_NAME="$2";          shift 2 ;;
        --package)           PACKAGE_ID="$2";        shift 2 ;;
        --skip-armeabi)      SKIP_ARMEABI=true;      shift ;;
        -h|--help)
            head -27 "$0" | tail -26
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
    OUTPUT="${PROJECT_ROOT}/build/android/Game.aab"
fi

STAGING_DIR="${PROJECT_ROOT}/build/android/aab_staging"
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

if ! command -v bundletool &>/dev/null; then
    echo "ERROR: bundletool not found in PATH."
    echo "  Install via:"
    echo "    brew install bundletool"
    echo "  Or download from:"
    echo "    https://github.com/google/bundletool/releases"
    ERRORS=1
fi

if ! command -v jarsigner &>/dev/null; then
    echo "ERROR: jarsigner not found in PATH (requires JDK)."
    echo "  Install Java 17+: brew install openjdk@17"
    ERRORS=1
fi

# Locate aapt2 from Android SDK build-tools
AAPT2=""
if [ -n "$ANDROID_SDK" ] && [ -d "$ANDROID_SDK" ]; then
    BUILD_TOOLS_DIR=""
    if [ -d "$ANDROID_SDK/build-tools" ]; then
        BUILD_TOOLS_DIR=$(ls -d "$ANDROID_SDK/build-tools"/*/ 2>/dev/null \
            | sort -V | tail -1 | sed 's:/$::')
    fi
    if [ -n "$BUILD_TOOLS_DIR" ]; then
        AAPT2="${BUILD_TOOLS_DIR}/aapt2"
    fi
fi
if [ ! -x "$AAPT2" ]; then
    AAPT2=$(command -v aapt2 2>/dev/null || true)
fi
if [ -z "$AAPT2" ]; then
    echo "ERROR: aapt2 not found."
    echo "  Install Android SDK build-tools:"
    echo "  sdkmanager --install 'build-tools;34.0.0'"
    ERRORS=1
fi

# Locate android.jar
ANDROID_JAR=""
if [ -n "$ANDROID_SDK" ] && [ -d "$ANDROID_SDK" ]; then
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

ABIS="arm64-v8a"
if [ "$SKIP_ARMEABI" = false ]; then
    ABIS="arm64-v8a armeabi-v7a"
fi

echo "=== Sama Engine — AAB Build ==="
echo "  Tier:        ${TIER}"
echo "  ABIs:        ${ABIS}"
echo "  Package:     ${PACKAGE_ID}"
echo "  App name:    ${APP_NAME}"
echo "  Output:      ${OUTPUT}"
if [ -n "$KEYSTORE" ]; then
    echo "  Keystore:    ${KEYSTORE}"
else
    echo "  Keystore:    (unsigned — sign before uploading to Play Store)"
fi
echo ""

# ── Step 1: Build native libraries for all ABIs ────────────────────────────

echo "[1/6] Building native libraries..."
for ABI in $ABIS; do
    echo "  Building ${ABI}..."
    "${PROJECT_ROOT}/android/build_android.sh" "$ABI" Release

    SO_PATH="${PROJECT_ROOT}/build/android/${ABI}/libsama_android.so"
    if [ ! -f "$SO_PATH" ]; then
        echo "ERROR: Native library not found at ${SO_PATH}"
        echo "  The NDK build may have failed."
        exit 1
    fi
done

# ── Step 2: Process assets ──────────────────────────────────────────────────

echo "[2/6] Processing assets for tier '${TIER}'..."
ASSET_TOOL="${PROJECT_ROOT}/build/sama_asset_tool"
ASSETS_DIR="${PROJECT_ROOT}/assets"
ASSETS_OUTPUT="${STAGING_DIR}/base/assets"

if [ "$CLEAN_STAGING" = true ]; then
    echo "[stage] Cleaning staging directory: ${STAGING_DIR}"
    rm -rf "$STAGING_DIR"
fi
mkdir -p "$ASSETS_OUTPUT"

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
    cp -r "$ASSETS_DIR"/* "$ASSETS_OUTPUT"/
else
    echo "  WARNING: No assets directory found at ${ASSETS_DIR}"
fi

# ── Step 3: Create base module staging structure ────────────────────────────

echo "[3/6] Staging AAB base module..."
mkdir -p "${STAGING_DIR}/base/manifest"
mkdir -p "${STAGING_DIR}/base/dex"

# Copy native libraries for each ABI
for ABI in $ABIS; do
    mkdir -p "${STAGING_DIR}/base/lib/${ABI}"
    cp "${PROJECT_ROOT}/build/android/${ABI}/libsama_android.so" \
       "${STAGING_DIR}/base/lib/${ABI}/libsama_android.so"
done

# Generate manifest from template
sed -e "s/com\\.sama\\.engine/${PACKAGE_ID//./\\.}/g" \
    -e "s/Sama Engine/${APP_NAME}/g" \
    "${PROJECT_ROOT}/android/AndroidManifest.xml" \
    > "${STAGING_DIR}/base/manifest/AndroidManifest.xml"

# ── Step 4: Compile resources and create base module zip ────────────────────

echo "[4/6] Creating base module..."
BASE_APK="${PROJECT_ROOT}/build/android/aab_base.apk"

# Use aapt2 to link the manifest and create a base APK with compiled resources
"$AAPT2" link -o "$BASE_APK" \
    --manifest "${STAGING_DIR}/base/manifest/AndroidManifest.xml" \
    -I "$ANDROID_JAR" \
    --proto-format

# Extract the proto-format APK to get compiled resources
PROTO_DIR="${PROJECT_ROOT}/build/android/aab_proto"
rm -rf "$PROTO_DIR"
mkdir -p "$PROTO_DIR"
unzip -q -o "$BASE_APK" -d "$PROTO_DIR"

# Copy compiled resources into the base module
if [ -f "$PROTO_DIR/resources.pb" ]; then
    cp "$PROTO_DIR/resources.pb" "${STAGING_DIR}/base/resources.pb"
fi
if [ -f "$PROTO_DIR/AndroidManifest.xml" ]; then
    cp "$PROTO_DIR/AndroidManifest.xml" "${STAGING_DIR}/base/manifest/AndroidManifest.xml"
fi

# Create native.pb and assets.pb (protobuf config files)
# bundletool expects these but they can be empty for simple bundles.
# The proto-format output from aapt2 already includes what we need.

# Create base.zip from the module directory
BASE_ZIP="${PROJECT_ROOT}/build/android/base.zip"
rm -f "$BASE_ZIP"
(cd "${STAGING_DIR}/base" && zip -r -q "$BASE_ZIP" .)

# ── Step 5: Build AAB with bundletool ──────────────────────────────────────

echo "[5/6] Building AAB with bundletool..."
mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"

bundletool build-bundle \
    --modules="$BASE_ZIP" \
    --output="$OUTPUT"

# ── Step 6: Sign the AAB (if keystore provided) ────────────────────────────

if [ -n "$KEYSTORE" ]; then
    echo "[6/6] Signing AAB..."
    if [ ! -f "$KEYSTORE" ]; then
        echo "ERROR: Keystore not found at ${KEYSTORE}"
        exit 1
    fi

    JARSIGNER_ARGS=(-keystore "$KEYSTORE")

    # Keystore password: env var takes precedence over literal --ks-pass.
    # jarsigner uses `-storepass:env NAME` for env-var lookup.
    if [ -n "$KS_PASS_ENV" ]; then
        if [ -z "${!KS_PASS_ENV:-}" ]; then
            echo "ERROR: --ks-pass-env ${KS_PASS_ENV} is unset or empty in environment."
            exit 1
        fi
        JARSIGNER_ARGS+=(-storepass:env "$KS_PASS_ENV")
    elif [ -n "$KS_PASS" ]; then
        JARSIGNER_ARGS+=(-storepass "$KS_PASS")
    fi

    # Key (alias) password: same precedence; falls back to keystore password
    # source if only the keystore password was supplied.
    if [ -n "$KEY_PASS_ENV" ]; then
        if [ -z "${!KEY_PASS_ENV:-}" ]; then
            echo "ERROR: --key-pass-env ${KEY_PASS_ENV} is unset or empty in environment."
            exit 1
        fi
        JARSIGNER_ARGS+=(-keypass:env "$KEY_PASS_ENV")
    elif [ -n "$KEY_PASS" ]; then
        JARSIGNER_ARGS+=(-keypass "$KEY_PASS")
    elif [ -n "$KS_PASS_ENV" ]; then
        JARSIGNER_ARGS+=(-keypass:env "$KS_PASS_ENV")
    elif [ -n "$KS_PASS" ]; then
        JARSIGNER_ARGS+=(-keypass "$KS_PASS")
    fi

    jarsigner "${JARSIGNER_ARGS[@]}" "$OUTPUT" "$KS_ALIAS"
else
    echo "[6/6] Skipping signing (no --keystore provided)."
    echo "  NOTE: AABs must be signed before uploading to Play Store."
    echo "  Sign later with:"
    echo "    jarsigner -keystore <path> ${OUTPUT} <alias>"
fi

# ── Clean up intermediate files ─────────────────────────────────────────────

rm -rf "$STAGING_DIR" "$PROTO_DIR"
rm -f "$BASE_APK" "$BASE_ZIP"

echo ""
echo "=== AAB built successfully ==="
echo "  Output: ${OUTPUT}"

# Report AAB size
if [ -f "$OUTPUT" ]; then
    SIZE=$(du -h "$OUTPUT" | cut -f1)
    echo "  Size:   ${SIZE}"
fi

echo ""
echo "To test the AAB locally with bundletool:"
echo "  bundletool build-apks --bundle=${OUTPUT} --output=Game.apks --local-testing"
echo "  bundletool install-apks --apks=Game.apks"
