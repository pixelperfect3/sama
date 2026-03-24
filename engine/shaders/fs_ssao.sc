$input v_texcoord0

#include <bgfx_shader.sh>

// ---------------------------------------------------------------------------
// SSAO fragment shader — Alchemy-style hemisphere sampling
//
// Inputs:
//   s_depth        — scene depth buffer (D24 / D32F), slot 9
//   u_invProj      — bgfx predefined inverse projection matrix (auto-injected)
//   u_proj         — bgfx predefined projection matrix (auto-injected)
//   u_ssaoKernel   — 16 hemisphere sample offsets in view space (vec4[16], w=0)
//   u_ssaoParams   — {radius, bias, power, sampleCount}
//
// Output:
//   gl_FragColor.r — occlusion factor in [0, 1]  (1 = fully occluded)
//
// NOTE: u_invProj and u_proj are bgfx predefined uniforms — do NOT redeclare
//       them with "uniform mat4".  bgfx injects them automatically into every
//       shader's uniform block; a second declaration causes a duplicate error.
// ---------------------------------------------------------------------------

SAMPLER2D(s_depth, 9);

uniform vec4 u_ssaoKernel[16];
uniform vec4 u_ssaoParams;

// Reconstruct view-space position from a depth-buffer sample.
// u_invProj is available as a bgfx predefined.
vec3 reconstructViewPos(vec2 uv, float depth)
{
    // Map [0,1] UV and depth into NDC.
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = mul(u_invProj, ndc);
    return view.xyz / view.w;
}

// Simple hash — produces a pseudo-random float in [0,1) from a 2-D seed.
float hash(vec2 p)
{
    float h = dot(p, vec2(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

void main()
{
    vec2 uv = v_texcoord0;

    float radius      = u_ssaoParams.x;
    float bias        = u_ssaoParams.y;
    float power       = u_ssaoParams.z;
    float sampleCount = u_ssaoParams.w;

    // Sample depth at the current fragment.
    float depth = texture2D(s_depth, uv).r;

    // Skip sky / far-plane fragments — nothing to occlude.
    if (depth >= 0.9999)
    {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    // Reconstruct view-space origin position.
    vec3 origin = reconstructViewPos(uv, depth);

    // Build a random rotation angle per-pixel to break up the kernel pattern.
    float angle = hash(uv) * 6.2831853;  // 2*pi
    float cosA  = cos(angle);
    float sinA  = sin(angle);

    // Approximate view-space normal via finite differences.
    vec2 texelSz = vec2(dFdx(uv.x), dFdy(uv.y));

    float depthR = texture2D(s_depth, uv + vec2(texelSz.x, 0.0)).r;
    float depthU = texture2D(s_depth, uv + vec2(0.0, texelSz.y)).r;

    vec3 posRight = reconstructViewPos(uv + vec2(texelSz.x, 0.0), depthR);
    vec3 posUp    = reconstructViewPos(uv + vec2(0.0, texelSz.y), depthU);

    vec3 N = normalize(cross(posRight - origin, posUp - origin));
    // Ensure normal points toward the camera (negative z in view space).
    N = N * sign(-N.z);

    // Build an orthonormal TBN with a random tangent orientation.
    vec3 randTan = vec3(cosA, sinA, 0.0);
    vec3 T = normalize(randTan - N * dot(randTan, N));
    vec3 B = cross(N, T);

    float occlusion = 0.0;
    float count     = 0.0;

    for (int i = 0; i < 16; ++i)
    {
        if (float(i) >= sampleCount)
            break;

        // Transform kernel sample from tangent space to view space.
        vec3 s        = u_ssaoKernel[i].xyz;
        vec3 sampleVS = origin + (s.x * T + s.y * B + s.z * N) * radius;

        // Project the sample position to UV space.
        vec4 sampleClip = mul(u_proj, vec4(sampleVS, 1.0));
        sampleClip.xyz /= sampleClip.w;

        vec2 sampleUV = clamp(sampleClip.xy * 0.5 + 0.5, vec2(0.0, 0.0), vec2(1.0, 1.0));

        float sd      = texture2D(s_depth, sampleUV).r;
        vec3  sampleP = reconstructViewPos(sampleUV, sd);

        // Range check: attenuate contribution for samples far from origin.
        float rangeCheck =
            smoothstep(0.0, 1.0, radius / (abs(origin.z - sampleP.z) + 0.0001));

        // Occluding when the actual surface is in front of the sample point.
        occlusion += (sampleP.z >= sampleVS.z + bias ? 1.0 : 0.0) * rangeCheck;
        count += 1.0;
    }

    if (count > 0.0)
        occlusion /= count;

    // Apply power curve: 0 = no occlusion, 1 = fully occluded.
    occlusion = pow(occlusion, power);

    gl_FragColor = vec4(occlusion, occlusion, occlusion, 1.0);
}
