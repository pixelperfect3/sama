$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

// Adapted from Eric Lengyel's "GPU Centered Glyph Rendering" reference
// implementation, MIT-licensed via https://github.com/EricLengyel/Slug.
//
// v1 Slug vertex shader. Uses the same vertex layout as the sprite shader
// so it can be driven by the existing UiRenderer without a layout change:
//   a_position  : screen-space quad corner (xy)
//   a_texcoord0 : glyph-local pixel coordinate (u = x in glyph, v = y)
//   a_color0    : RGBA tint; see SlugFont.h "Integration note" — in a full
//                 integration the .zw channels would carry the per-glyph
//                 (curveOffset, curveCount) pair packed as floats.

#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_viewProj, vec4(a_position.xy, 0.0, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
}
