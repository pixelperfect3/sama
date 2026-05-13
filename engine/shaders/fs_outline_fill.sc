$input v_texcoord0

#include <bgfx_shader.sh>

// Stencil-fill fragment shader for the editor selection outline.
// The draw call disables color writes (BGFX_STATE_WRITE_RGB / WRITE_A both
// off) — this fragment shader exists only so bgfx has a valid program to
// pair with vs_outline_fill.  The output value is irrelevant.
void main()
{
    gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
}
