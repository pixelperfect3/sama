$input a_position
$output v_worldPos

// Skybox vertex shader.
//
// Driven by a 3-vertex fullscreen triangle in clip space:
//   (-1, -1), (3, -1), (-1, 3)
// — this single triangle covers the full viewport with no overdraw and is
// the standard bgfx fullscreen-pass shape.
//
// We unproject the near-plane point at each vertex through the inverse
// view-projection matrix to recover its world-space position, then
// subtract the camera world position (recovered from u_invView's
// translation column) to get a direction from the camera into the world.
// The fragment shader uses that direction to sample the cubemap.
//
// gl_Position.z = gl_Position.w forces fragment depth to exactly 1.0
// after the perspective divide so the skybox sits at the far plane.
// Combined with depth test = LESS_EQUAL and depth write = OFF, the
// skybox only fills pixels where opaque geometry hasn't already drawn.

#include <bgfx_shader.sh>

void main()
{
    gl_Position = vec4(a_position.xy, 1.0, 1.0);

    // Unproject the near-plane point at this vertex to a world-space
    // position, then subtract the camera origin to get a direction.
    vec4 nearWorld = mul(u_invViewProj, vec4(a_position.xy, 0.0, 1.0));
    nearWorld.xyz /= nearWorld.w;

    // Camera world position is the translation column of u_invView.
    vec3 camPos = vec3(u_invView[3][0], u_invView[3][1], u_invView[3][2]);

    v_worldPos = nearWorld.xyz - camPos;
}
