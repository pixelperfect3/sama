$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdrColor, 0);   // current (smaller) level
SAMPLER2D(s_bloomPrev, 1);  // previous (larger) level to add
uniform vec4 u_bloomParams;  // .y=intensity (bloom strength)
uniform vec4 u_texelSize;

void main()
{
    vec2 ts = u_texelSize.xy;
    vec3 up = texture2D(s_hdrColor, v_texcoord0).rgb;
    up = (up + texture2D(s_hdrColor, v_texcoord0 + vec2(ts.x, 0.0)).rgb
             + texture2D(s_hdrColor, v_texcoord0 + vec2(0.0, ts.y)).rgb
             + texture2D(s_hdrColor, v_texcoord0 + ts).rgb) * 0.25;
    vec3 prev = texture2D(s_bloomPrev, v_texcoord0).rgb;
    gl_FragColor = vec4(prev + up * u_bloomParams.y, 1.0);
}
