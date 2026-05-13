$input v_texcoord0

#include <bgfx_shader.sh>

// Outline fragment shader — emits a constant colour pulled from
// u_outlineColor.  The actual silhouette shape is produced by the stencil
// test (NOT_EQUAL 1) configured on the draw call: pixels covered by the
// preceding stencil-fill pass are masked out, leaving only the inflated
// silhouette band visible.
//
// u_outlineColor  — RGBA outline colour (linear).  Editor uses bright
//                   yellow/orange so it pops against any background.

uniform vec4 u_outlineColor;

void main()
{
    gl_FragColor = u_outlineColor;
}
