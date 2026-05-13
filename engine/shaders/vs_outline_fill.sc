$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

// Stencil-fill vertex shader for the editor selection outline.
// Position-only stream — emits clip-space position from u_modelViewProj.
// Pairs with fs_outline_fill.sc; the draw is configured with stencil-write
// state (REF=1, OP_PASS=REPLACE) and color writes disabled so the only
// observable side-effect is stencil_value <- 1 wherever the visible mesh
// surface lies.  v_texcoord0 is unused but required by varying.def.sc.
void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_texcoord0 = vec2(0.0, 0.0);
}
