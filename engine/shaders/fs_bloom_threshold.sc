$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdrColor, 0);
uniform vec4 u_bloomParams;  // .x=threshold, .y=intensity

void main()
{
    vec3 color = texture2D(s_hdrColor, v_texcoord0).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float w = max(brightness - u_bloomParams.x, 0.0) / max(brightness, 0.0001);
    gl_FragColor = vec4(color * w, 1.0);
}
