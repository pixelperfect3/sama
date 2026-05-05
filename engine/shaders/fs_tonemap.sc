$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdrColor, 0);
SAMPLER2D(s_bloomTex, 1);
uniform vec4 u_bloomParams;  // .x=threshold, .y=intensity

void main()
{
    vec3 hdr   = texture2D(s_hdrColor, v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloomTex, v_texcoord0).rgb;
    // Multiply bloom by u_bloomParams.y (intensity) so the bloom-disabled
    // path — where PostProcessSystem binds s_bloomTex to s_hdrColor and sets
    // intensity to 0 — actually drops the bloom contribution.  Without this
    // multiply the shader doubled the scene brightness on every frame.
    // When bloom is enabled, PostProcessSystem sets intensity = 1 here
    // because the bloom upsample passes already baked intensity into the
    // bloom buffer; multiplying again would be double-counting.
    vec3 color = hdr + bloom * u_bloomParams.y;
    // ACES filmic (Narkowicz approximation):
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
    color = pow(color, vec3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}
