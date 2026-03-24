#include <bgfx_shader.sh>

// Depth-only pass — fragment colour output is unused.
// bgfx requires a fragment shader even for depth-only passes.

void main()
{
    // Intentionally empty — only depth writes are needed.
}
