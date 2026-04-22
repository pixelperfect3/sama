#!/bin/bash
# ---------------------------------------------------------------------------
# Compile engine shaders to SPIRV for Android (Vulkan).
#
# Uses the desktop-built shaderc binary to cross-compile .sc shader sources
# into .bin files suitable for bgfx on Vulkan/Android.
#
# Usage: ./android/compile_shaders.sh [--all]
#   Default: compile only the minimum shaders needed for UiRenderer + BitmapFont
#   --all:   compile all engine shaders
#
# The output .bin files go into assets/shaders/spirv/ and are packaged into
# the APK by build_apk.sh.
# ---------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Locate shaderc — prefer the one built by CMake in build/
SHADERC="${PROJECT_ROOT}/build/shaderc"
if [ ! -x "$SHADERC" ]; then
    SHADERC="${PROJECT_ROOT}/build/bin/shaderc"
fi
if [ ! -x "$SHADERC" ]; then
    # bgfx_cmake builds shaderc in its own subdirectory
    SHADERC="${PROJECT_ROOT}/build/_deps/bgfx_cmake-build/shaderc"
fi
if [ ! -x "$SHADERC" ]; then
    # bkaradzic/bgfx.cmake puts shaderc under cmake/bgfx/
    SHADERC="${PROJECT_ROOT}/build/_deps/bgfx_cmake-build/cmake/bgfx/shaderc"
fi
if [ ! -x "$SHADERC" ]; then
    SHADERC=$(command -v shaderc 2>/dev/null || true)
fi
if [ -z "$SHADERC" ] || [ ! -x "$SHADERC" ]; then
    echo "ERROR: shaderc not found."
    echo "  Build it first: cmake --build build --target shaderc -j\$(sysctl -n hw.ncpu)"
    exit 1
fi

SHADER_DIR="${PROJECT_ROOT}/engine/shaders"
VARYING="${SHADER_DIR}/varying.def.sc"
OUT_DIR="${PROJECT_ROOT}/assets/shaders/spirv"

# bgfx include dirs needed by shaderc for bgfx_shader.sh etc.
BGFX_INCLUDE="${PROJECT_ROOT}/build/_deps/bgfx_cmake-src/bgfx/src"
if [ ! -d "$BGFX_INCLUDE" ]; then
    # Fallback: try common FetchContent path
    BGFX_INCLUDE=$(find "${PROJECT_ROOT}/build/_deps" -name "bgfx_shader.sh" -exec dirname {} \; 2>/dev/null | head -1)
fi

mkdir -p "$OUT_DIR"

COMPILE_ALL=false
if [[ "${1:-}" == "--all" ]]; then
    COMPILE_ALL=true
fi

compile_shader() {
    local TYPE=$1      # vertex or fragment
    local INPUT=$2     # full path to .sc file
    local OUTPUT=$3    # full path to output .bin file
    local NAME
    NAME=$(basename "$INPUT")

    echo "  $TYPE  $NAME -> $(basename "$OUTPUT")"
    "$SHADERC" \
        -f "$INPUT" \
        -o "$OUTPUT" \
        --type "$TYPE" \
        --platform linux \
        -p spirv \
        --varyingdef "$VARYING" \
        -i "$BGFX_INCLUDE" \
        -i "$SHADER_DIR"
}

echo "=== Compiling shaders to SPIRV for Android ==="
echo "  shaderc: $SHADERC"
echo "  output:  $OUT_DIR"
echo ""

# Minimum set: sprite + rounded_rect + msdf (for UiRenderer + BitmapFont + MsdfFont)
echo "[Minimum] UiRenderer shaders:"
compile_shader vertex   "$SHADER_DIR/vs_sprite.sc"        "$OUT_DIR/vs_sprite.bin"
compile_shader fragment "$SHADER_DIR/fs_sprite.sc"        "$OUT_DIR/fs_sprite.bin"
compile_shader vertex   "$SHADER_DIR/vs_rounded_rect.sc"  "$OUT_DIR/vs_rounded_rect.bin"
compile_shader fragment "$SHADER_DIR/fs_rounded_rect.sc"  "$OUT_DIR/fs_rounded_rect.bin"
compile_shader fragment "$SHADER_DIR/fs_msdf.sc"          "$OUT_DIR/fs_msdf.bin"

if [ "$COMPILE_ALL" = true ]; then
    echo ""
    echo "[All] Remaining shaders:"
    compile_shader vertex   "$SHADER_DIR/vs_unlit.sc"          "$OUT_DIR/vs_unlit.bin"
    compile_shader fragment "$SHADER_DIR/fs_unlit.sc"          "$OUT_DIR/fs_unlit.bin"
    compile_shader vertex   "$SHADER_DIR/vs_pbr.sc"            "$OUT_DIR/vs_pbr.bin"
    compile_shader fragment "$SHADER_DIR/fs_pbr.sc"            "$OUT_DIR/fs_pbr.bin"
    compile_shader vertex   "$SHADER_DIR/vs_pbr_skinned.sc"    "$OUT_DIR/vs_pbr_skinned.bin"
    compile_shader vertex   "$SHADER_DIR/vs_shadow.sc"         "$OUT_DIR/vs_shadow.bin"
    compile_shader fragment "$SHADER_DIR/fs_shadow.sc"         "$OUT_DIR/fs_shadow.bin"
    compile_shader vertex   "$SHADER_DIR/vs_shadow_skinned.sc" "$OUT_DIR/vs_shadow_skinned.bin"
    compile_shader vertex   "$SHADER_DIR/vs_gizmo.sc"          "$OUT_DIR/vs_gizmo.bin"
    compile_shader fragment "$SHADER_DIR/fs_gizmo.sc"          "$OUT_DIR/fs_gizmo.bin"
    compile_shader vertex   "$SHADER_DIR/vs_skybox.sc"         "$OUT_DIR/vs_skybox.bin"
    compile_shader fragment "$SHADER_DIR/fs_skybox.sc"         "$OUT_DIR/fs_skybox.bin"
    compile_shader fragment "$SHADER_DIR/fs_msdf.sc"           "$OUT_DIR/fs_msdf.bin"
    compile_shader vertex   "$SHADER_DIR/vs_slug.sc"           "$OUT_DIR/vs_slug.bin"
    compile_shader fragment "$SHADER_DIR/fs_slug.sc"           "$OUT_DIR/fs_slug.bin"

    # Post-processing shaders (may not exist yet — compile if present)
    for NAME in vs_postprocess fs_postprocess vs_ssao fs_ssao vs_blur fs_blur; do
        SRC="$SHADER_DIR/${NAME}.sc"
        if [ -f "$SRC" ]; then
            TYPE="vertex"
            if [[ "$NAME" == fs_* ]]; then TYPE="fragment"; fi
            compile_shader "$TYPE" "$SRC" "$OUT_DIR/${NAME}.bin"
        fi
    done
fi

echo ""
echo "=== Done. Compiled shaders are in: $OUT_DIR ==="
