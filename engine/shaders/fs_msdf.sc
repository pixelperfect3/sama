$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// ---------------------------------------------------------------------------
// fs_msdf — Multi-channel signed distance field text reconstruction.
//
// Samples the 3-channel MSDF atlas, takes the per-pixel median of the three
// distance channels, then converts to opacity via smoothstep based on the
// distance range baked into the atlas at generation time.
//
// MsdfFont::bindResources() sets u_msdfParams.x = distanceRange (pixels).
// For a first cut we pass it directly; a more accurate implementation would
// scale by the screen-pixel size of the glyph (requestedSize / nominalSize).
// ---------------------------------------------------------------------------

SAMPLER2D(s_texture, 0);

// .x = pixelRange (distanceRange, in atlas pixels)
uniform vec4 u_msdfParams;

float msdfMedian(vec3 c)
{
    return max(min(c.r, c.g), min(max(c.r, c.g), c.b));
}

void main()
{
    vec3 msd = texture2D(s_texture, v_texcoord0).rgb;
    float sd = msdfMedian(msd);
    float screenPxDistance = u_msdfParams.x * (sd - 0.5);
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * opacity);
}
