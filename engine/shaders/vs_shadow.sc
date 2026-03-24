$input a_position
#include <bgfx_shader.sh>

// Depth-only shadow pass — Stream 0 (position) only.
// u_modelViewProj is provided by bgfx_shader.sh (model * view * proj).

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
