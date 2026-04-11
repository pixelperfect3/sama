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
    // The IBL cubemap stores linear HDR; tonemap with a simple Reinhard so
    // the editor scene exposure matches the rest of the PBR pipeline output.
    vec3 mapped = sky.rgb / (sky.rgb + vec3(1.0, 1.0, 1.0));
    gl_FragColor = vec4(mapped, 1.0);
}
