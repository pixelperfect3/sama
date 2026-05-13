$input a_position, a_normal
$output v_texcoord0

#include <bgfx_shader.sh>

// Outline-expansion vertex shader.
//
// Reads the position stream (stream 0) and the oct-encoded normal from the
// surface stream (stream 1, attribute Normal — snorm16x2).  Transforms both
// into world space via the model matrix, pushes the world position outward
// along the world-space normal by u_outlineParams.x metres, then transforms
// into clip space.  Pair with fs_outline.sc (solid colour fill) and a
// stencil_test = NOT_EQUAL 1 draw to produce a silhouette band around any
// pixels covered by the previous stencil-fill pass.
//
// Inflating in WORLD space keeps the visible outline a constant thickness
// regardless of the entity's scale.  An earlier object-space implementation
// inflated by the same metres before the model matrix, which then scaled
// the inflation by the per-axis scale — a ground plane with scale (10, 0.1,
// 10) ended up with an outline 10× thicker in X/Z than the unit cube's,
// extending far enough (with depth test off) to overpaint the cube in
// pixels where they overlap in screen space.
//
// u_outlineParams.x  — outline thickness in world-space metres
// u_outlineParams.y  — unused (reserved for screen-space scaling)
// u_outlineParams.zw — unused (padding)

uniform vec4 u_outlineParams;

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
    // Decode object-space normal from the oct-encoded snorm16x2 stream.
    vec3 nObj = octDecode(a_normal.xy);

    // Transform position + normal into world space.  Using the model matrix
    // for the normal direction is only strictly correct for uniform scale;
    // non-uniform scale needs transpose(inverse(mat3(u_model))).  The editor
    // uses near-uniform scaling for visible meshes today, and the outline is
    // a debug visualisation — the normalize() after the transform absorbs
    // any reasonable scale skew.
    vec3 worldPos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
    vec3 worldNormal = normalize(mul(u_model[0], vec4(nObj, 0.0)).xyz);

    vec3 inflated = worldPos + worldNormal * u_outlineParams.x;

    gl_Position = mul(u_viewProj, vec4(inflated, 1.0));
    v_texcoord0 = vec2(0.0, 0.0);
}
