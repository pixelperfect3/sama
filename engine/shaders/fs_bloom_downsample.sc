$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_hdrColor, 0);
uniform vec4 u_texelSize;  // .xy = 1/width, 1/height of SOURCE texture

void main()
{
    vec2 uv = v_texcoord0;
    vec2 ts = u_texelSize.xy;
    vec3 c = texture2D(s_hdrColor, uv + vec2(-ts.x, -ts.y)).rgb * 0.25
           + texture2D(s_hdrColor, uv + vec2( ts.x, -ts.y)).rgb * 0.25
           + texture2D(s_hdrColor, uv + vec2(-ts.x,  ts.y)).rgb * 0.25
           + texture2D(s_hdrColor, uv + vec2( ts.x,  ts.y)).rgb * 0.25;
    gl_FragColor = vec4(c, 1.0);
}
