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
#   --asset-pack <name>:<dir>   Add an install-time asset pack module. The
#                               source directory is copied into the pack at
#                               assets/<name>/. Repeatable. Pack name must be
#                               a valid split id (alphanumeric + underscore),
#                               must not be 'base', and the source dir must
#                               exist. Per-pack uncompressed size is capped at
#                               1.5 GB by Play Store; the script enforces this
#                               and errors out before invoking bundletool.
#                               Note: only install-time packs are supported.
#                               Fast-follow / on-demand packs require Play
#                               Core's AssetPackManager (Java) which is not
#                               wired up — see ANDROID_SUPPORT.md Phase H.
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

# Parallel arrays of asset pack names and source dirs.
ASSET_PACK_NAMES=()
ASSET_PACK_DIRS=()

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
        --asset-pack)
            # Format: name:source-dir. The colon separates the pack name (a
            # valid split id) from a path. The path itself may contain
            # colons on macOS volumes named with one, so we split on the
            # FIRST colon only.
            PACK_SPEC="$2"
            if [[ "$PACK_SPEC" != *:* ]]; then
                echo "ERROR: --asset-pack expects 'name:source-dir', got '${PACK_SPEC}'"
                exit 1
            fi
            PACK_NAME="${PACK_SPEC%%:*}"
            PACK_DIR="${PACK_SPEC#*:}"
            ASSET_PACK_NAMES+=("$PACK_NAME")
            ASSET_PACK_DIRS+=("$PACK_DIR")
            shift 2
            ;;
        -h|--help)
            head -39 "$0" | tail -38
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

# ── Validate asset packs ────────────────────────────────────────────────────
#
# Play Store rules (as of 2026):
#   - Per-pack uncompressed size limit: 1.5 GB
#   - Total install-time delivery (base + all install-time packs): 4 GB
# We enforce the per-pack limit strictly and warn on the total. The split
# id rules come from the AAPT2 manifest validator: alphanumeric + '_',
# must start with a letter or underscore, must not collide with 'base'.

PER_PACK_BYTES_LIMIT=$((1610612736))   # 1.5 GiB
TOTAL_PACK_BYTES_WARN=$((4294967296))  # 4 GiB
TOTAL_PACK_BYTES=0

for i in "${!ASSET_PACK_NAMES[@]}"; do
    PACK_NAME="${ASSET_PACK_NAMES[$i]}"
    PACK_DIR="${ASSET_PACK_DIRS[$i]}"

    if [ -z "$PACK_NAME" ]; then
        echo "ERROR: --asset-pack name is empty (spec '${PACK_NAME}:${PACK_DIR}')"
        exit 1
    fi
    if [ "$PACK_NAME" = "base" ]; then
        echo "ERROR: --asset-pack name 'base' is reserved for the base module."
        exit 1
    fi
    if ! [[ "$PACK_NAME" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        echo "ERROR: --asset-pack name '${PACK_NAME}' is not a valid split id."
        echo "  Allowed: letters, digits, underscore; must start with a letter or underscore."
        exit 1
    fi
    # Detect duplicate pack names (would clobber each other in the AAB).
    for j in "${!ASSET_PACK_NAMES[@]}"; do
        if [ "$j" -lt "$i" ] && [ "${ASSET_PACK_NAMES[$j]}" = "$PACK_NAME" ]; then
            echo "ERROR: --asset-pack name '${PACK_NAME}' is specified more than once."
            exit 1
        fi
    done
    if [ ! -d "$PACK_DIR" ]; then
        echo "ERROR: --asset-pack source dir '${PACK_DIR}' (pack '${PACK_NAME}') does not exist."
        exit 1
    fi

    # Size check — uncompressed bytes on disk. macOS `du` lacks GNU's
    # --bytes; -k gives KiB which we multiply.
    PACK_KB=$(du -sk "$PACK_DIR" 2>/dev/null | cut -f1)
    PACK_BYTES=$((PACK_KB * 1024))
    if [ "$PACK_BYTES" -gt "$PER_PACK_BYTES_LIMIT" ]; then
        echo "ERROR: asset pack '${PACK_NAME}' is ${PACK_BYTES} bytes (~$((PACK_BYTES / 1024 / 1024)) MiB)."
        echo "  Play Store limit per pack is 1.5 GiB (${PER_PACK_BYTES_LIMIT} bytes)."
        echo "  Split this pack across multiple --asset-pack arguments."
        exit 1
    fi
    TOTAL_PACK_BYTES=$((TOTAL_PACK_BYTES + PACK_BYTES))
done

if [ "$TOTAL_PACK_BYTES" -gt "$TOTAL_PACK_BYTES_WARN" ]; then
    echo "WARNING: total asset-pack size is $((TOTAL_PACK_BYTES / 1024 / 1024)) MiB,"
    echo "  which exceeds Play Store's 4 GiB cap on install-time delivery (base + packs)."
    echo "  The build will continue, but the upload will be rejected by Play Console."
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
if [ "${#ASSET_PACK_NAMES[@]}" -gt 0 ]; then
    echo "  Asset packs: ${#ASSET_PACK_NAMES[@]} install-time pack(s)"
    for i in "${!ASSET_PACK_NAMES[@]}"; do
        PACK_KB=$(du -sk "${ASSET_PACK_DIRS[$i]}" 2>/dev/null | cut -f1)
        echo "    - ${ASSET_PACK_NAMES[$i]}  (${ASSET_PACK_DIRS[$i]}, $((PACK_KB / 1024)) MiB)"
    done
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

# ── Step 4b: Stage and zip install-time asset pack modules ─────────────────
#
# Each asset pack is its own bundle module with the structure:
#
#   <pack_name>/
#     manifest/AndroidManifest.xml   (declares dist:type="asset-pack" +
#                                     dist:install-time delivery)
#     assets/<pack_name>/...         (the actual asset files)
#
# At install time on the device, Play Store delivers all install-time
# packs alongside the base APK and merges them so the assets appear at
# the same /data/app/.../base.apk!/assets/<pack_name>/ paths the runtime
# already reads via AAssetManager. No engine code change needed — the
# AndroidFileSystem path resolution is unchanged.
#
# Pack manifests must be aapt2-linked into proto format the same way the
# base manifest is, otherwise bundletool rejects them with an "expected
# proto-format manifest" error.

PACK_ZIPS=()
PACK_APKS=()
PACK_PROTO_DIRS=()
if [ "${#ASSET_PACK_NAMES[@]}" -gt 0 ]; then
    echo "[4b/6] Staging ${#ASSET_PACK_NAMES[@]} asset pack module(s)..."
    for i in "${!ASSET_PACK_NAMES[@]}"; do
        PACK_NAME="${ASSET_PACK_NAMES[$i]}"
        PACK_DIR="${ASSET_PACK_DIRS[$i]}"
        PACK_STAGE="${STAGING_DIR}/${PACK_NAME}"

        echo "  - ${PACK_NAME}: copying assets..."
        rm -rf "$PACK_STAGE"
        mkdir -p "${PACK_STAGE}/manifest" "${PACK_STAGE}/assets/${PACK_NAME}"
        # Copy the source dir contents into assets/<pack_name>/.
        # Preserve directory structure but strip the source path itself.
        cp -R "$PACK_DIR"/. "${PACK_STAGE}/assets/${PACK_NAME}/"

        # Write the pack manifest. The dist namespace + asset-pack module
        # type + install-time delivery are what tell bundletool (and the
        # Play Store) this is an install-time asset pack.
        PACK_MANIFEST="${PACK_STAGE}/manifest/AndroidManifest.xml"
        cat > "$PACK_MANIFEST" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
          xmlns:dist="http://schemas.android.com/apk/distribution"
          package="${PACKAGE_ID}"
          split="${PACK_NAME}">
    <dist:module dist:type="asset-pack">
        <dist:fusing dist:include="true"/>
        <dist:delivery>
            <dist:install-time/>
        </dist:delivery>
    </dist:module>
</manifest>
EOF

        # aapt2-link the pack manifest into proto format.
        PACK_APK="${PROJECT_ROOT}/build/android/aab_${PACK_NAME}.apk"
        PACK_PROTO_DIR="${PROJECT_ROOT}/build/android/aab_${PACK_NAME}_proto"
        rm -rf "$PACK_PROTO_DIR"
        mkdir -p "$PACK_PROTO_DIR"
        "$AAPT2" link -o "$PACK_APK" \
            --manifest "$PACK_MANIFEST" \
            -I "$ANDROID_JAR" \
            --proto-format
        unzip -q -o "$PACK_APK" -d "$PACK_PROTO_DIR"
        if [ -f "$PACK_PROTO_DIR/AndroidManifest.xml" ]; then
            cp "$PACK_PROTO_DIR/AndroidManifest.xml" "$PACK_MANIFEST"
        fi

        # Zip the pack module.
        PACK_ZIP="${PROJECT_ROOT}/build/android/${PACK_NAME}.zip"
        rm -f "$PACK_ZIP"
        (cd "$PACK_STAGE" && zip -r -q "$PACK_ZIP" .)

        PACK_ZIPS+=("$PACK_ZIP")
        PACK_APKS+=("$PACK_APK")
        PACK_PROTO_DIRS+=("$PACK_PROTO_DIR")
    done
fi

# ── Step 5: Build AAB with bundletool ──────────────────────────────────────

echo "[5/6] Building AAB with bundletool..."
mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"

# Comma-separated list of all module zips (base + asset packs).
MODULES_ARG="$BASE_ZIP"
for PACK_ZIP in "${PACK_ZIPS[@]}"; do
    MODULES_ARG="${MODULES_ARG},${PACK_ZIP}"
done

bundletool build-bundle \
    --modules="$MODULES_ARG" \
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
for PACK_APK in "${PACK_APKS[@]}"; do rm -f "$PACK_APK"; done
for PACK_PROTO_DIR in "${PACK_PROTO_DIRS[@]}"; do rm -rf "$PACK_PROTO_DIR"; done
for PACK_ZIP in "${PACK_ZIPS[@]}"; do rm -f "$PACK_ZIP"; done

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
