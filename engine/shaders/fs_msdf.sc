$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// ---------------------------------------------------------------------------
// fs_msdf — Multi-channel signed distance field text reconstruction.
//
// Samples the 3-channel MSDF atlas, takes the per-pixel median of the three
// distance channels, then converts to opacity via a signed-distance smoothstep.
//
// The canonical msdf-atlas-gen technique scales the atlas distance range
// (in atlas pixels) by the ratio of screen pixels to atlas texels, so edges
// stay crisp at any draw size. fwidth(v_texcoord0) gives the per-screen-pixel
// rate of change in UV space, which lets us derive how many screen pixels a
// single atlas texel currently covers on screen.
//
// MsdfFont::bindResources() sets:
//   u_msdfParams.x  = pxRange (atlas distanceRange, in atlas pixels)
//   u_msdfParams.yz = atlas texture dimensions (width, height) in pixels
// ---------------------------------------------------------------------------

SAMPLER2D(s_texture, 0);

// .x = pxRange, .yz = atlas size (px), .w = unused
uniform vec4 u_msdfParams;

float msdfMedian(vec3 c)
{
    return max(min(c.r, c.g), min(max(c.r, c.g), c.b));
}

float msdfScreenPxRange(vec2 uv)
{
    vec2 unitRange = vec2(u_msdfParams.x, u_msdfParams.x) / u_msdfParams.yz;
    vec2 screenTexSize = vec2(1.0, 1.0) / fwidth(uv);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main()
{
    vec3 msd = texture2D(s_texture, v_texcoord0).rgb;
    float sd = msdfMedian(msd);
    float screenPxDistance = msdfScreenPxRange(v_texcoord0) * (sd - 0.5);
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * opacity);
}
