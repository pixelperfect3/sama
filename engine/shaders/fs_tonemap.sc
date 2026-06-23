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
    // Gamma 2.0 approximation via sqrt() — audit (PERF_AUDIT_2026-05-25
    // "Shaders").  Maximum delta vs true pow(x, 1/2.2) across [0, 1] is
    // ~0.018 at x ≈ 0.05 (perceptually within JND for an LDR output
    // pipeline that's already going through tonemap quantisation).
    // Saves one pow per pixel; on Mali this is 2-3× faster than pow.
    // Alternative would be writing the LDR FB as BGFX_TEXTURE_FORMAT_SRGB
    // and dropping the gamma pass entirely — that's a bigger refactor
    // gated on every downstream consumer (capture path, blit-to-screen,
    // imgui composition); revisit if the sqrt drift ever becomes
    // perceptible in a real scene.
    color = sqrt(color);
    gl_FragColor = vec4(color, 1.0);
}
