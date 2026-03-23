$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_worldPos

#include <bgfx_shader.sh>

void main()
{
    // Instance matrix from i_data0..i_data3; each vec4 is one row of the matrix.
    // Row-assignment form is portable across all bgfx GLSL/HLSL/Metal backends.
    mat4 model;
    model[0] = i_data0;
    model[1] = i_data1;
    model[2] = i_data2;
    model[3] = i_data3;
    vec4 worldPos = instMul(model, vec4(a_position, 1.0));
    v_worldPos    = worldPos.xyz;
    gl_Position   = mul(u_viewProj, worldPos);
}
