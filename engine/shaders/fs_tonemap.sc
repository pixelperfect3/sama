$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdrColor, 0);
SAMPLER2D(s_bloomTex, 1);
uniform vec4 u_bloomParams;  // .x=threshold, .y=intensity

void main()
{
    vec3 hdr   = texture2D(s_hdrColor, v_texcoord0).rgb;
    vec3 bloom = texture2D(s_bloomTex, v_texcoord0).rgb;
    vec3 color = hdr + bloom;
    // ACES filmic (Narkowicz approximation):
    color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, vec3(0.0, 0.0, 0.0), vec3(1.0, 1.0, 1.0));
    color = pow(color, vec3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}
