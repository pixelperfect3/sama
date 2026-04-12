$input a_position, a_texcoord0, a_color0, a_texcoord1
$output v_texcoord0, v_color0, v_uirect

// Rounded-rect vertex shader.
//
// Same vertex stream layout as the sprite shader (pos2 + uv2 + color4u8)
// plus an extra TEXCOORD1 vec4 carrying per-rect SDF parameters:
//   x = half-width  (screen pixels)
//   y = half-height (screen pixels)
//   z = corner radius (screen pixels)
//   w = unused, reserved
//
// All 4 vertices of one rect share the same a_texcoord1 value. The
// fragment shader uses (uv * 2 - 1) * (halfW, halfH) to recover its
// position relative to the rect center, then runs the standard rounded
// box SDF and uses the result as an antialiased alpha mask.

#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_viewProj, vec4(a_position.xy, 0.0, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0    = a_color0;
    v_uirect    = a_texcoord1;
}
