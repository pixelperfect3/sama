#!/bin/bash
# ---------------------------------------------------------------------------
# Compile engine shaders to SPIRV for Android (Vulkan).
#
# Uses the desktop-built shaderc binary to cross-compile .sc shader sources
# into .bin files suitable for bgfx on Vulkan/Android.
#
# Usage: ./android/compile_shaders.sh [--minimum]
#   Default:    compile all engine shaders (UI + PBR + shadows + post-process)
#   --minimum:  compile only the minimum shaders needed for UiRenderer + BitmapFont
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
VARYING_PP="${SHADER_DIR}/varying_pp.def.sc"
OUT_DIR="${PROJECT_ROOT}/assets/shaders/spirv"

# bgfx include dirs needed by shaderc for bgfx_shader.sh etc.
BGFX_INCLUDE="${PROJECT_ROOT}/build/_deps/bgfx_cmake-src/bgfx/src"
if [ ! -d "$BGFX_INCLUDE" ]; then
    # Fallback: try common FetchContent path
    BGFX_INCLUDE=$(find "${PROJECT_ROOT}/build/_deps" -name "bgfx_shader.sh" -exec dirname {} \; 2>/dev/null | head -1)
fi

mkdir -p "$OUT_DIR"

# Default behaviour: compile every shader. Pass --minimum to compile only the
# UiRenderer / BitmapFont set (useful for a quick rebuild).
COMPILE_ALL=true
if [[ "${1:-}" == "--minimum" ]]; then
    COMPILE_ALL=false
elif [[ "${1:-}" == "--all" ]]; then
    # Accept --all for backward compatibility (it is now the default).
    COMPILE_ALL=true
fi

# compile_shader <type> <input.sc> <output.bin> [varying_def_path]
# If varying_def_path is omitted, the standard varying.def.sc is used.
compile_shader() {
    local TYPE=$1      # vertex or fragment
    local INPUT=$2     # full path to .sc file
    local OUTPUT=$3    # full path to output .bin file
    local VDEF=${4:-$VARYING}
    local NAME
    NAME=$(basename "$INPUT")

    echo "  $TYPE  $NAME -> $(basename "$OUTPUT")"
    "$SHADERC" \
        -f "$INPUT" \
        -o "$OUTPUT" \
        --type "$TYPE" \
        --platform linux \
        -p spirv \
        --varyingdef "$VDEF" \
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
    echo "[Scene] Unlit / PBR / shadow / instanced / skinned shaders:"
    compile_shader vertex   "$SHADER_DIR/vs_unlit.sc"          "$OUT_DIR/vs_unlit.bin"
    compile_shader fragment "$SHADER_DIR/fs_unlit.sc"          "$OUT_DIR/fs_unlit.bin"
    compile_shader vertex   "$SHADER_DIR/vs_pbr.sc"            "$OUT_DIR/vs_pbr.bin"
    compile_shader fragment "$SHADER_DIR/fs_pbr.sc"            "$OUT_DIR/fs_pbr.bin"
    compile_shader vertex   "$SHADER_DIR/vs_pbr_skinned.sc"    "$OUT_DIR/vs_pbr_skinned.bin"
    compile_shader vertex   "$SHADER_DIR/vs_shadow.sc"         "$OUT_DIR/vs_shadow.bin"
    compile_shader fragment "$SHADER_DIR/fs_shadow.sc"         "$OUT_DIR/fs_shadow.bin"
    compile_shader vertex   "$SHADER_DIR/vs_shadow_skinned.sc" "$OUT_DIR/vs_shadow_skinned.bin"
    compile_shader vertex   "$SHADER_DIR/vs_instanced.sc"      "$OUT_DIR/vs_instanced.bin"
    compile_shader vertex   "$SHADER_DIR/vs_gizmo.sc"          "$OUT_DIR/vs_gizmo.bin"
    compile_shader fragment "$SHADER_DIR/fs_gizmo.sc"          "$OUT_DIR/fs_gizmo.bin"
    compile_shader vertex   "$SHADER_DIR/vs_skybox.sc"         "$OUT_DIR/vs_skybox.bin"
    compile_shader fragment "$SHADER_DIR/fs_skybox.sc"         "$OUT_DIR/fs_skybox.bin"
    compile_shader vertex   "$SHADER_DIR/vs_slug.sc"           "$OUT_DIR/vs_slug.bin"
    compile_shader fragment "$SHADER_DIR/fs_slug.sc"           "$OUT_DIR/fs_slug.bin"

    echo ""
    echo "[Post-process] fullscreen vertex + bloom / tonemap / fxaa / ssao:"
    # Post-process shaders use a different varying def (vec2 v_uv only).
    compile_shader vertex   "$SHADER_DIR/vs_fullscreen.sc"       "$OUT_DIR/vs_fullscreen.bin"     "$VARYING_PP"
    compile_shader fragment "$SHADER_DIR/fs_bloom_threshold.sc"  "$OUT_DIR/fs_bloom_threshold.bin" "$VARYING_PP"
    compile_shader fragment "$SHADER_DIR/fs_bloom_downsample.sc" "$OUT_DIR/fs_bloom_downsample.bin" "$VARYING_PP"
    compile_shader fragment "$SHADER_DIR/fs_bloom_upsample.sc"   "$OUT_DIR/fs_bloom_upsample.bin"   "$VARYING_PP"
    compile_shader fragment "$SHADER_DIR/fs_tonemap.sc"          "$OUT_DIR/fs_tonemap.bin"          "$VARYING_PP"
    compile_shader fragment "$SHADER_DIR/fs_fxaa.sc"             "$OUT_DIR/fs_fxaa.bin"             "$VARYING_PP"
    compile_shader fragment "$SHADER_DIR/fs_ssao.sc"             "$OUT_DIR/fs_ssao.bin"             "$VARYING_PP"
fi

echo ""
echo "=== Done. Compiled shaders are in: $OUT_DIR ==="
