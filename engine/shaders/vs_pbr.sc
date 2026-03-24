$input a_position, a_normal, a_tangent, a_texcoord0
$output v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0

#include <bgfx_shader.sh>

// u_model[BGFX_CONFIG_MAX_BONES] and u_modelViewProj are provided by bgfx_shader.sh.
// We use u_model[0] for the world transform.

// Oct-decode: maps a snorm16 vec2 in [-1, 1] back to a unit vec3.
// The w-component of the octahedron is 1 - |x| - |y|; when negative the
// sign of the input components is used to recover the correct hemisphere.
vec3 octDecode(vec2 f)
{
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.x += (n.x >= 0.0) ? t : -t;
    n.y += (n.y >= 0.0) ? t : -t;
    return normalize(n);
}

void main()
{
    // World position using bgfx's built-in u_model[0].
    vec4 worldPos = mul(u_model[0], vec4(a_position, 1.0));
    v_worldPos = worldPos.xyz;

    // Clip position.
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));

    // Decode oct-encoded normal (snorm16 → [-1, 1] already done by the
    // bgfx vertex attribute normalisation flag on attrib Normal).
    vec3 N = octDecode(a_normal.xy);

    // Decode oct-encoded tangent.  The tangent XY are stored as unorm8
    // (remapped to [0,1]); we remap to [-1,1].  The W component encodes
    // the bitangent sign: after unorm decode, 0 → -1 and 1 → +1.
    vec2 tanOct = a_tangent.xy * 2.0 - 1.0;
    vec3 T = octDecode(tanOct);
    float bitangentSign = a_tangent.w * 2.0 - 1.0;

    // Extract upper-left 3×3 from the model matrix.
    // Phase 3 assumes uniform scale — no inverse-transpose needed.
    // bgfx uses column-major storage; [col][row] in GLSL, [row][col] in HLSL.
    // bgfx_shader.sh normalises this via macros so use row extraction:
    mat3 m3 = mtxFromCols3(u_model[0][0].xyz, u_model[0][1].xyz, u_model[0][2].xyz);

    v_normal = normalize(mul(m3, N));
    v_tangent = normalize(mul(m3, T));
    v_bitangent = cross(v_normal, v_tangent) * bitangentSign;

    v_texcoord0 = a_texcoord0;
}
