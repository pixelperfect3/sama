$input v_texcoord0, v_color0

// Adapted from Eric Lengyel's "GPU Centered Glyph Rendering" reference
// implementation, MIT-licensed via https://github.com/EricLengyel/Slug.
//
// v1 Slug fragment shader — minimum-viable coverage evaluation.
//
// This is NOT the full banded implementation from the paper. For a first
// cut we do the simple, unoptimized version:
//   * The curve buffer for the glyph is addressed via `u_slugParams.xy`
//     = (curveOffset, curveCount) set by the CPU per draw. When integrated
//     with a Slug-aware UiRenderer, this will move into vertex attributes
//     so that a single draw can batch many glyphs. See SLUG_NEXT_STEPS.md.
//   * For each quadratic Bezier curve (p0, p1, p2) we intersect the
//     horizontal line y = fragment.y with the curve and count signed
//     crossings on fragment.x's right side. Odd winding = inside the glyph.
//   * Coverage is hardened (no AA) for v1. Real Slug evaluates a
//     per-pixel windowed integral for AA; that comes later.
//
// Curve buffer layout (must match SlugFont::kFloatsPerCurve = 8):
//   texel 2n+0 = (p0.x, p0.y, p1.x, p1.y)
//   texel 2n+1 = (p2.x, p2.y, 0,    0   )
// The texture is RGBA32F, sampled with texelFetch-style coordinates via
// modular math on `u_slugCurvesDim.xy`.

#include <bgfx_shader.sh>

SAMPLER2D(s_slugCurves, 1);
uniform vec4 u_slugParams;     // (curveOffset, curveCount, 0, 0)
uniform vec4 u_slugCurvesDim;  // (width, height, 1/width, 1/height)

vec4 fetchTexel(int idx)
{
    int w = int(u_slugCurvesDim.x);
    int tx = idx - (idx / w) * w;
    int ty = idx / w;
    vec2 uv = (vec2(float(tx), float(ty)) + vec2(0.5, 0.5)) * u_slugCurvesDim.zw;
    return texture2DLod(s_slugCurves, uv, 0.0);
}

// Solve q(t) = (1-t)^2 p0 + 2(1-t)t p1 + t^2 p2 = y for t in [0,1].
// Returns the number of valid roots in `out t0` / `out t1`.
int solveQuadratic(float a, float b, float c, out float t0, out float t1)
{
    t0 = -1.0; t1 = -1.0;
    // a*t^2 + b*t + c = 0
    if (abs(a) < 1e-6)
    {
        if (abs(b) < 1e-6) return 0;
        t0 = -c / b;
        return 1;
    }
    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return 0;
    float s = sqrt(disc);
    t0 = (-b - s) / (2.0 * a);
    t1 = (-b + s) / (2.0 * a);
    return 2;
}

void main()
{
    vec2 frag = v_texcoord0;  // glyph-local pixel coord
    int  curveOffset = int(u_slugParams.x);
    int  curveCount  = int(u_slugParams.y);

    int winding = 0;
    for (int i = 0; i < 512; ++i)
    {
        if (i >= curveCount) break;
        vec4 a = fetchTexel((curveOffset + i) * 2 + 0);
        vec4 b = fetchTexel((curveOffset + i) * 2 + 1);
        vec2 p0 = a.xy;
        vec2 p1 = a.zw;
        vec2 p2 = b.xy;

        // y(t) = (p0.y - 2 p1.y + p2.y) t^2 + 2 (p1.y - p0.y) t + p0.y
        float qa = p0.y - 2.0 * p1.y + p2.y;
        float qb = 2.0 * (p1.y - p0.y);
        float qc = p0.y - frag.y;

        float t0, t1;
        int n = solveQuadratic(qa, qb, qc, t0, t1);
        for (int k = 0; k < 2; ++k)
        {
            float t = (k == 0) ? t0 : t1;
            if (k >= n) break;
            if (t < 0.0 || t > 1.0) continue;
            float omt = 1.0 - t;
            float x = omt * omt * p0.x + 2.0 * omt * t * p1.x + t * t * p2.x;
            if (x > frag.x)
            {
                // Sign from tangent direction (dy/dt).
                float dy = 2.0 * omt * (p1.y - p0.y) + 2.0 * t * (p2.y - p1.y);
                winding += (dy > 0.0) ? 1 : -1;
            }
        }
    }

    float alpha = (winding != 0) ? 1.0 : 0.0;
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * alpha);
}
