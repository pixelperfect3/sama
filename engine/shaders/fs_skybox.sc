$input v_worldPos

// Skybox fragment shader. The vertex shader emits the unit-cube vertex
// position through `v_worldPos`; here we treat it as a direction from the
// camera into the world and sample the cubemap. Mip 0 of the prefiltered
// IBL cubemap is the "actual" environment, so we sample it directly with
// no LOD bias.

#include <bgfx_shader.sh>

SAMPLERCUBE(s_skybox, 0);

void main()
{
    vec3 dir = normalize(v_worldPos);
    vec4 sky = textureCube(s_skybox, dir);
    // The IBL cubemap stores linear HDR; output it directly.
    // Tonemap + sRGB gamma are owned by PostProcessSystem so any inline
    // Reinhard here would double-correct (washed-out / overbright sky).
    gl_FragColor = vec4(sky.rgb, 1.0);
}
