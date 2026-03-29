$input a_position, a_indices, a_weight
#define SKINNED 1
#include <bgfx_shader.sh>

// Skinned variant of the shadow depth-only vertex shader.
// A separate .sc file is needed because bgfx's $input directive does not
// support preprocessor conditionals.

void main()
{
#if SKINNED
    vec4 weights = a_weight;
    int idx0 = int(a_indices.x);
    int idx1 = int(a_indices.y);
    int idx2 = int(a_indices.z);
    int idx3 = int(a_indices.w);
    mat4 skinMatrix = weights.x * u_model[idx0]
                    + weights.y * u_model[idx1]
                    + weights.z * u_model[idx2]
                    + weights.w * u_model[idx3];
    gl_Position = mul(u_viewProj, mul(skinMatrix, vec4(a_position, 1.0)));
#else
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
#endif
}
