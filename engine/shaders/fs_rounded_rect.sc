$input v_texcoord0, v_color0, v_uirect

// Rounded-rect fragment shader.
//
// Recovers the fragment's position relative to the rect center (in pixels)
// from the interpolated UV — corners are (0,0)..(1,1) so (uv*2-1) gives
// values in [-1, 1] over the rect, multiplied by the half-size from
// v_uirect.xy yields a pixel-space offset from center.
//
// The standard rounded-box SDF (Inigo Quilez) returns a signed distance:
// negative inside the shape, zero on the edge, positive outside. We use
// fwidth() on the SDF value to derive an antialiased opacity mask via
// smoothstep, so the corners stay smooth at any draw size without aliasing.
//
// When v_uirect.z (the corner radius) is 0, the math degenerates to a
// plain rectangle SDF, which still produces clean antialiased edges —
// so this shader is also a strict upgrade for sharp rects when called.

#include <bgfx_shader.sh>

SAMPLER2D(s_texture, 0);

float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r, r);
    return length(max(q, vec2(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 halfSize = v_uirect.xy;
    float radius = v_uirect.z;

    // Recover pixel-space position relative to the rect center.
    vec2 pos = (v_texcoord0 * 2.0 - vec2(1.0, 1.0)) * halfSize;

    float sdf = sdRoundBox(pos, halfSize, radius);

    // fwidth gives the per-pixel rate of change of sdf — use it as the
    // smoothstep half-width so the antialiased edge is exactly 1 pixel
    // wide regardless of draw size.
    float aa = fwidth(sdf);
    float mask = 1.0 - smoothstep(-aa, aa, sdf);

    vec4 texColor = texture2D(s_texture, v_texcoord0);
    gl_FragColor = vec4(texColor.rgb * v_color0.rgb, texColor.a * v_color0.a * mask);
}
