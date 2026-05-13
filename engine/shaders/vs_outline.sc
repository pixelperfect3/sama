$input a_position, a_normal
$output v_texcoord0

#include <bgfx_shader.sh>

// Outline-expansion vertex shader.
//
// Reads the position stream (stream 0) and the oct-encoded normal from the
// surface stream (stream 1, attribute Normal — snorm16x2).  The position is
// pushed outward along the world-space normal by u_outlineParams.x metres,
// then transformed into clip space.  Pair with fs_outline.sc (solid colour
// fill) and a stencil_test = NOT_EQUAL 1 draw to produce a silhouette band
// around any pixels covered by the previous stencil-fill pass.
//
// u_outlineParams.x  — outline thickness in world-space units
// u_outlineParams.y  — unused (reserved for screen-space scaling)
// u_outlineParams.zw — unused (padding)

uniform vec4 u_outlineParams;

vec3 octDecode(vec2 f)
{
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

void main()
{
    // Decode object-space normal from the oct-encoded snorm16x2 stream.
    vec3 nObj = octDecode(a_normal.xy);

    // Inflate the position along the object-space normal by the requested
    // thickness.  Doing the offset in object space (before the model matrix)
    // keeps the visual width consistent under non-uniform world scale far
    // better than offsetting in clip space.
    vec3 inflated = a_position + nObj * u_outlineParams.x;

    gl_Position = mul(u_modelViewProj, vec4(inflated, 1.0));
    v_texcoord0 = vec2(0.0, 0.0);
}
