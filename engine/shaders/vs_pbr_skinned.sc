$input a_position, a_normal, a_tangent, a_texcoord0, a_indices, a_weight
$output v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos

#define SKINNED 1

#include <bgfx_shader.sh>

// Skinned variant of the PBR vertex shader.
// The shader body is identical to vs_pbr.sc; SKINNED=1 activates the
// bone-blending path.  A separate .sc file is needed because bgfx's $input
// directive does not support preprocessor conditionals.

// Oct-decode: maps a snorm16 vec2 in [-1, 1] back to a unit vec3.
vec3 octDecode(vec2 f)
{
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? -t : t;
    n.y += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

void main()
{
#if SKINNED
    // Skinned path: blend bone matrices from u_model[] using vertex weights.
    vec4 weights = a_weight;
    int idx0 = int(a_indices.x);
    int idx1 = int(a_indices.y);
    int idx2 = int(a_indices.z);
    int idx3 = int(a_indices.w);
    mat4 skinMatrix = weights.x * u_model[idx0]
                    + weights.y * u_model[idx1]
                    + weights.z * u_model[idx2]
                    + weights.w * u_model[idx3];

    vec4 worldPos = mul(skinMatrix, vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPos);

    // Extract upper-left 3x3 from the skin matrix for normal transform.
    mat3 m3 = mtxFromCols3(skinMatrix[0].xyz, skinMatrix[1].xyz, skinMatrix[2].xyz);
#else
    // Static mesh path: single world transform from u_model[0].
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

    mat3 m3 = mtxFromCols3(u_model[0][0].xyz, u_model[0][1].xyz, u_model[0][2].xyz);
#endif

    v_worldPos = worldPos.xyz;

    // Decode oct-encoded normal.
    vec3 N = octDecode(a_normal.xy);

    // Decode oct-encoded tangent.
    vec2 tanOct = a_tangent.xy * 2.0 - 1.0;
    vec3 T = octDecode(tanOct);
    float bitangentSign = a_tangent.w * 2.0 - 1.0;

    v_normal = normalize(mul(m3, N));
    v_tangent = normalize(mul(m3, T));
    v_bitangent = cross(v_normal, v_tangent) * bitangentSign;

    v_texcoord0 = a_texcoord0;

    // View-space position for cluster depth slice.
    v_viewPos = mul(u_view, worldPos).xyz;
}
