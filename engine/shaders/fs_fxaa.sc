$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_ldrColor, 0);
uniform vec4 u_texelSize;

void main()
{
    vec2 uv = v_texcoord0;
    vec2 rcpFrame = u_texelSize.xy;
    // Luma at current pixel and neighbours:
    float lumaNW = dot(texture2D(s_ldrColor, uv + vec2(-rcpFrame.x, -rcpFrame.y)).rgb,
                       vec3(0.299, 0.587, 0.114));
    float lumaNE = dot(texture2D(s_ldrColor, uv + vec2( rcpFrame.x, -rcpFrame.y)).rgb,
                       vec3(0.299, 0.587, 0.114));
    float lumaSW = dot(texture2D(s_ldrColor, uv + vec2(-rcpFrame.x,  rcpFrame.y)).rgb,
                       vec3(0.299, 0.587, 0.114));
    float lumaSE = dot(texture2D(s_ldrColor, uv + vec2( rcpFrame.x,  rcpFrame.y)).rgb,
                       vec3(0.299, 0.587, 0.114));
    float lumaM  = dot(texture2D(s_ldrColor, uv).rgb, vec3(0.299, 0.587, 0.114));
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    vec2 dir = vec2(-(lumaNW + lumaNE) + (lumaSW + lumaSE),
                     (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * 0.0625, 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0, -8.0), vec2(8.0, 8.0)) * rcpFrame;
    vec3 rgbA = 0.5 * (texture2D(s_ldrColor, uv + dir * (1.0 / 3.0 - 0.5)).rgb
                     + texture2D(s_ldrColor, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture2D(s_ldrColor, uv + dir * -0.5).rgb
                                    + texture2D(s_ldrColor, uv + dir *  0.5).rgb);
    float lumaB = dot(rgbB, vec3(0.299, 0.587, 0.114));
    gl_FragColor = vec4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0);
}
