$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

// Full-screen triangle vertex shader.
// Vertices are pre-transformed in clip space (no model/view/proj multiply needed).
// Call with a 3-vertex buffer containing:
//   v0 = (-1, -1, 0)  → UV (0, 1)
//   v1 = ( 3, -1, 0)  → UV (2, 1)
//   v2 = (-1,  3, 0)  → UV (0, -1)
// This single triangle covers the entire NDC square.
void main()
{
    gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0);
    // Derive UV from clip-space position; flip Y for Metal/Vulkan conventions.
    v_texcoord0 = vec2(a_position.x * 0.5 + 0.5, 0.5 - a_position.y * 0.5);
}
