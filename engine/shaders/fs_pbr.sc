$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos

#include <bgfx_shader.sh>

// ---------------------------------------------------------------------------
// Precision policy — see docs/PERF_AUDIT_2026-05-25.md item #S1.
//
// `mediump` is applied to:
//   - colour/material samples (8-bit textures, no precision win from highp);
//   - BRDF helper return types (D / G / F operate on small bounded values);
//   - per-light BRDF intermediates (NdotL2, kD2, spec2, ...);
//   - light colour + cone factors (the rest of the per-light data needs highp);
//   - IBL irradiance / prefiltered / BRDF-LUT samples (all colour);
//   - the radiance accumulator (Lo) and ambient/emissive sums.
//
// `highp` is kept (default) for:
//   - world / view-space positions, distance(), camera position;
//   - all texture coordinates and cluster math (indices stored as floats
//     would lose precision above 2048 in fp16);
//   - shadow map UV (sub-pixel filter alignment).
//
// On targets that don't honour the qualifiers — desktop GLSL 1.20 ignores
// them, HLSL/Metal treat them as hints — the source is still legal because
// glslang accepts `mediump`/`highp` as no-ops on those profiles.  On ESSL
// (Android GLES) the qualifier picks fp16 hardware paths; on SPIRV it
// emits `RelaxedPrecision` decorations that SPIRV-Cross translates to
// `half` types in MSL and that Mali / Adreno drivers honour as fp16.
// ---------------------------------------------------------------------------

// [0] = {albedo.rgb, roughness}   [1] = {metallic, emissiveScale, 0, 0}
uniform vec4 u_material[2];

// [0] = {dir.xyz, 0}   [1] = {color.rgb * intensity, 0}
// dir points from the surface toward the light source (normalised, away from surface).
uniform vec4 u_dirLight[2];

// [0] = {viewportWidth, viewportHeight, near, far}   [1] = {camPos.x, camPos.y, camPos.z, 0}
uniform vec4 u_frameParams[2];

// Clustered lighting (Phase 6):
//   .x = numLights, .y = screenW, .z = screenH, .w = unused
uniform vec4 u_lightParams;

SAMPLER2D(s_albedo,    0);
SAMPLER2D(s_normal,    1);
SAMPLER2D(s_orm,       2);
SAMPLER2D(s_emissive,  3);
SAMPLER2D(s_occlusion, 4);
SAMPLER2DSHADOW(s_shadowMap, 5);

// Light data textures — Phase 6.
// s_lightData:  256x4 RGBA32F  (row 0=pos/r, row 1=col/type, row 2=dir/cosOuter, row 3=cosInner)
// s_lightGrid:  3456x1 RGBA32F (x=offset, y=count per cluster)
// s_lightIndex: 8192x1 R32F    (flat light index list)
SAMPLER2D(s_lightData,  12);
SAMPLER2D(s_lightGrid,  13);
SAMPLER2D(s_lightIndex, 14);

// IBL textures — Phase 11.
SAMPLERCUBE(s_irradiance,  6);
SAMPLERCUBE(s_prefiltered, 7);
SAMPLER2D(s_brdfLut,       8);

// .x = maxMipLevels for prefiltered cubemap, .y = iblEnabled (1.0=on, 0.0=off)
uniform vec4 u_iblParams;

// mat4[4]: one shadow matrix per cascade (worldPos -> shadow UV).
uniform mat4 u_shadowMatrix[4];

// vec4: per-cascade split distances (.x = cascade 0 far; Phase 4 uses only .x).
uniform vec4 u_cascadeSplits;

#define PI 3.14159265358979

// GGX / Trowbridge-Reitz normal distribution function.
// All inputs are unit-vec dot products or roughness in [0.04, 1] — the
// safest mediump targets in the whole shader.
mediump float distributionGGX(mediump vec3 N, mediump vec3 H, mediump float roughness)
{
    mediump float a = roughness * roughness;
    mediump float a2 = a * a;
    mediump float NdotH = max(dot(N, H), 0.0);
    mediump float NdotH2 = NdotH * NdotH;
    mediump float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Schlick-GGX geometry term for a single direction.
mediump float geometrySchlick(mediump float NdotV, mediump float roughness)
{
    mediump float r = roughness + 1.0;
    mediump float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith geometry function — product of two Schlick-GGX terms.
mediump float geometrySmith(mediump vec3 N, mediump vec3 V, mediump vec3 L,
                            mediump float roughness)
{
    return geometrySchlick(max(dot(N, V), 0.0), roughness)
         * geometrySchlick(max(dot(N, L), 0.0), roughness);
}

// Fresnel-Schlick approximation.
mediump vec3 fresnelSchlick(mediump float cosTheta, mediump vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    // -----------------------------------------------------------------------
    // Material inputs — all from 8-bit textures, no precision win from highp.
    // -----------------------------------------------------------------------
    mediump vec3 albedo = u_material[0].xyz;
    mediump float roughness = max(u_material[0].w, 0.04);
    mediump float metallic = u_material[1].x;
    mediump float emissiveScale = u_material[1].y;

    // Albedo map (slot 0).  Default white when no texture is bound.
    mediump vec4 albedoSample = texture2D(s_albedo, v_texcoord0);
    albedo *= albedoSample.xyz;

    // ORM map (slot 2): G=roughness, B=metallic.  R is not used — AO comes from s_occlusion.
    mediump vec4 ormSample = texture2D(s_orm, v_texcoord0);
    roughness *= ormSample.y;
    metallic  *= ormSample.z;

    // Occlusion map (slot 4): R=AO.  Defaults to white (ao=1.0) when no texture is bound.
    // Floor at 0.1 to prevent fully-black ambient in the absence of IBL.
    mediump float ao = max(texture2D(s_occlusion, v_texcoord0).x, 0.1);

    // -----------------------------------------------------------------------
    // Normal — TBN normal mapping.  TBN frame stays highp; normalSample
    // (from an 8-bit texture) is mediump.  The final world-space N is
    // mediump (unit vector — fp16 representation has ~10 bits of
    // precision per axis, well within the angular accuracy that PBR
    // shading needs).
    // -----------------------------------------------------------------------
    vec3 T = normalize(v_tangent);
    vec3 B = normalize(v_bitangent);
    vec3 Ngeom = normalize(v_normal);

    // Sample normal map (slot 1), decode [0,1] -> [-1,1] tangent-space normal.
    mediump vec3 normalSample = texture2D(s_normal, v_texcoord0).xyz;
    normalSample = normalSample * 2.0 - 1.0;

    // Transform tangent-space normal to world space.
    // Expanded manually (T*ns.x + B*ns.y + N*ns.z) to avoid
    // mtxFromCols which transposes on Metal's shader path.
    mediump vec3 N =
        normalize(T * normalSample.x + B * normalSample.y + Ngeom * normalSample.z);

    // -----------------------------------------------------------------------
    // View vector: camera world position from u_frameParams[1].xyz.
    // camPos / v_worldPos are highp (positions can be far from origin); V
    // is mediump after normalize (unit vector).
    // -----------------------------------------------------------------------
    vec3 camPos = u_frameParams[1].xyz;
    mediump vec3 V = normalize(camPos - v_worldPos);

    // -----------------------------------------------------------------------
    // F0 — dielectric base (0.04) lerped to albedo for metals.  Bounded by
    // the [0.04, albedo] range, well inside mediump.
    // -----------------------------------------------------------------------
    mediump vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);

    // -----------------------------------------------------------------------
    // Directional light contribution — every quantity here is unit vec,
    // bounded BRDF factor, or colour radiance.  All mediump-safe.
    // -----------------------------------------------------------------------
    mediump vec3 L = normalize(u_dirLight[0].xyz);  // points toward the light
    mediump vec3 H = normalize(V + L);
    mediump vec3 radiance = u_dirLight[1].xyz;

    // Use normal-mapped N for both diffuse and specular to ensure energy
    // conservation and consistent Cook-Torrance denominator cancellation.
    mediump float NdotL = max(dot(N, L), 0.0);
    mediump float NdotV = max(dot(N, V), 0.0);

    mediump float D = distributionGGX(N, H, roughness);
    mediump float G = geometrySmith(N, V, L, roughness);
    mediump vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Diffuse: energy conservation — metals have no diffuse term.
    mediump vec3 kD = (vec3_splat(1.0) - F) * (1.0 - metallic);

    // Cook-Torrance specular BRDF.
    mediump vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    // Outgoing radiance from the directional light.  Lo is the per-fragment
    // colour accumulator; bounded in practice by the HDR colour budget of
    // RGBA16F, well inside mediump's 65504 ceiling.
    mediump vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;


    // -----------------------------------------------------------------------
    // Shadow — PCF 2x2, cascade 0 (Phase 4: single directional cascade).
    // Default shadow = 1.0 (fully lit) when:
    //   - u_shadowMatrix[0] is the zero matrix (w = 0 after transform), or
    //   - shadow UV is outside the [0,1]^3 frustum (fragment beyond shadow coverage).
    // Both cases occur in tests and scenes without an active shadow pass.
    // -----------------------------------------------------------------------
    // Shadow scalar is bounded in [0, 1] — mediump is fine.  shadowCoord
    // and texelSize stay highp: PCF tap alignment cares about sub-pixel
    // UV accuracy and fp16 would alias at 2048-res shadow maps.
    mediump float shadow = 1.0;
    vec4 shadowCoord = mul(u_shadowMatrix[0], vec4(v_worldPos, 1.0));
    if (shadowCoord.w > 0.0001)
    {
        shadowCoord.xyz /= shadowCoord.w;
        if (shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
            shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0 &&
            shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0)
        {
            float texelSize = 1.0 / 2048.0;
            // Slope-scaled bias: surfaces at oblique angles to the light need
            // more bias.  Clamp to [0.002, 0.02] to avoid light leaks.
            mediump float slopeFactor = clamp(1.0 - dot(Ngeom, L), 0.0, 1.0);
            mediump float shadowBias = mix(0.002, 0.02, slopeFactor);
            float shadowZ = shadowCoord.z - shadowBias;
            shadow  = shadow2D(s_shadowMap, vec3(shadowCoord.xy + vec2(-texelSize, -texelSize), shadowZ));
            shadow += shadow2D(s_shadowMap, vec3(shadowCoord.xy + vec2( texelSize, -texelSize), shadowZ));
            shadow += shadow2D(s_shadowMap, vec3(shadowCoord.xy + vec2(-texelSize,  texelSize), shadowZ));
            shadow += shadow2D(s_shadowMap, vec3(shadowCoord.xy + vec2( texelSize,  texelSize), shadowZ));
            shadow *= 0.25;
        }
    }

    Lo *= shadow;

    // -----------------------------------------------------------------------
    // Clustered point/spot light loop (Phase 6)
    //
    // 1. Determine which cluster this fragment belongs to using its tile
    //    position and log-depth slice.
    // 2. Look up the (offset, count) pair for that cluster in s_lightGrid.
    // 3. For each assigned light index, fetch light data from s_lightData and
    //    accumulate PBR contribution.
    //
    // Light positions are stored in view space (LightClusterBuilder transforms
    // them during collectLights). v_viewPos is the fragment position in view
    // space, output by vs_pbr.sc.
    // -----------------------------------------------------------------------

    float nearPlane = u_frameParams[0].z;
    float farPlane  = u_frameParams[0].w;
    float screenW   = u_lightParams.y;
    float screenH   = u_lightParams.z;

    // Tile index (integer, 0-based).
    vec2 tile = floor(gl_FragCoord.xy / vec2(screenW / 16.0, screenH / 9.0));

    // Positive view-space depth (v_viewPos.z is negative in right-handed view).
    float viewZ = -v_viewPos.z;

    // Clamp to valid range before log — avoids NaN/Inf at or behind near plane.
    viewZ = clamp(viewZ, nearPlane, farPlane);

    // Logarithmic depth slice index.
    float logRatio   = log(farPlane / nearPlane);
    float depthSlice = log(viewZ / nearPlane) / logRatio * 24.0;
    int sliceIdx     = int(clamp(depthSlice, 0.0, 23.0));

    int clusterIdx = sliceIdx * 144 + int(tile.y) * 16 + int(tile.x);

    // Guard against out-of-range clusters (fragments near screen edge).
    clusterIdx = clamp(clusterIdx, 0, 3455);

    // Sample the grid texture: x=offset, y=count.
    vec4 gridEntry = texture2D(s_lightGrid,
        vec2((float(clusterIdx) + 0.5) / 3456.0, 0.5));
    int lightOffset = int(gridEntry.x);
    int lightCount  = int(gridEntry.y);

    for (int li = 0; li < lightCount; ++li)
    {
        // Index sampling stays at default precision (highp) — fp16 stores
        // integers exactly only up to 2048, and the flat light index can
        // reach 8191.
        float idx = texture2D(s_lightIndex,
            vec2((float(lightOffset + li) + 0.5) / 8192.0, 0.5)).x;
        int i = int(idx);

        // Fetch the four rows of light data.  Row 0 (view-space position +
        // radius) stays highp — distance() against the radius must be
        // accurate at the radius boundary, and view-space positions span
        // tens of metres.  Rows 1-3 (colour, cone factors, spot direction)
        // are all small-bounded values; mediump is safe.
        vec4         ld0 = texture2D(s_lightData,
            vec2((float(i) + 0.5) / 256.0, 0.5 / 4.0));
        mediump vec4 ld1 = texture2D(s_lightData,
            vec2((float(i) + 0.5) / 256.0, 1.5 / 4.0));
        mediump vec4 ld2 = texture2D(s_lightData,
            vec2((float(i) + 0.5) / 256.0, 2.5 / 4.0));
        mediump vec4 ld3 = texture2D(s_lightData,
            vec2((float(i) + 0.5) / 256.0, 3.5 / 4.0));

        vec3          lightViewPos = ld0.xyz;
        float         lightRad     = ld0.w;
        mediump vec3  lightColor   = ld1.xyz;
        mediump float lightType    = ld1.w;

        // Vector from fragment (view space) to light (view space).  Highp:
        // position subtraction, then highp length() — both before the
        // radius cull, where precision matters most.
        vec3  Lv   = lightViewPos - v_viewPos;
        float dist = length(Lv);

        if (dist >= lightRad)
            continue;

        // After the cull we know |Lv| < lightRad.  The unit vector is
        // mediump-safe; downstream BRDF math expects mediump anyway.
        mediump vec3 Ln = normalize(Lv);

        // Inverse-square attenuation — ratio in [0, 1], so mediump throughout.
        mediump float ratio = dist / lightRad;
        mediump float att = clamp(1.0 - ratio * ratio, 0.0, 1.0);
        att *= att;

        // Spot cone attenuation (type == 1).
        mediump float spotAtt = 1.0;
        if (lightType > 0.5)
        {
            mediump vec3  spotDir  = ld2.xyz;
            mediump float cosOuter = ld2.w;
            mediump float cosInner = ld3.x;
            mediump float cosAngle = dot(-Ln, normalize(spotDir));
            spotAtt = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.0001),
                            0.0, 1.0);
        }

        // PBR contribution — same shape as the directional path, all the
        // same bounded BRDF factors and unit vectors.
        mediump vec3  H2     = normalize(V + Ln);
        mediump float NdotL2 = max(dot(N, Ln), 0.0);
        mediump float D2     = distributionGGX(N, H2, roughness);
        mediump float G2     = geometrySmith(N, V, Ln, roughness);
        mediump vec3  F2     = fresnelSchlick(max(dot(H2, V), 0.0), F0);
        mediump vec3  kD2    = (vec3_splat(1.0) - F2) * (1.0 - metallic);
        mediump vec3  spec2  =
            D2 * G2 * F2 / max(4.0 * max(dot(N, V), 0.0) * NdotL2, 0.001);

        Lo += (kD2 * albedo / PI + spec2) * lightColor * att * spotAtt * NdotL2;
    }

    // -----------------------------------------------------------------------
    // Ambient — IBL split-sum (Phase 11).  Every term is a colour sample or
    // bounded scalar; all mediump-safe.
    // -----------------------------------------------------------------------
    mediump vec3 ambient;
    if (u_iblParams.y > 0.5)
    {
        // Diffuse IBL: sample irradiance cubemap in the surface normal direction.
        mediump vec3 irradiance = textureCube(s_irradiance, N).rgb;
        mediump vec3 diffuse    = irradiance * albedo * (1.0 - metallic);

        // Specular IBL (split-sum approximation).  Reflection vector is a
        // unit vec (fp16 angular precision is enough for the prefiltered
        // cubemap LOD); mipLevel is in [0, kMaxMipLevels=~9]; brdf is from
        // an 8-bit LUT — everything is mediump-safe.
        mediump vec3  R                = reflect(-V, N);
        mediump float mipLevel         = roughness * u_iblParams.x;
        mediump vec3  prefilteredColor = textureCubeLod(s_prefiltered, R, mipLevel).rgb;
        mediump vec2  brdf             =
            texture2D(s_brdfLut, vec2(max(dot(N, V), 0.0), roughness)).xy;
        mediump vec3  specIbl          = prefilteredColor * (F0 * brdf.x + brdf.y);

        ambient = (diffuse + specIbl) * ao;
    }
    else
    {
        // Hemisphere ambient: cool sky from above, warm ground from below.
        // Use the smooth geometry normal (Ngeom) for the hemisphere factor so
        // the ambient varies smoothly with the object shape, not with the
        // high-frequency normal map.  F0-weighted term gives metallic surfaces
        // their colour from indirect light (replaces IBL irradiance/prefilter).
        mediump vec3  skyColor    = vec3(0.15, 0.18, 0.25);
        mediump vec3  groundColor = vec3(0.05, 0.04, 0.03);
        mediump float hemiFactor  = Ngeom.y * 0.5 + 0.5;
        mediump vec3  hemi        = mix(groundColor, skyColor, hemiFactor);
        ambient = (hemi * albedo * (1.0 - metallic) + hemi * F0) * ao;
    }

    // -----------------------------------------------------------------------
    // Emissive contribution.
    // Sample s_emissive (slot 3); defaults to white when no texture is bound.
    // emissiveScale is 0 for non-emissive materials, so white * 0 = no contribution.
    // -----------------------------------------------------------------------
    mediump vec3 emissive = texture2D(s_emissive, v_texcoord0).rgb * emissiveScale;

    // -----------------------------------------------------------------------
    // Output linear HDR.  Tonemap + gamma are owned by PostProcessSystem's
    // tonemap pass (fs_tonemap.sc, ACES) so the shader cannot double-correct.
    // -----------------------------------------------------------------------
    mediump vec3 color = ambient + Lo + emissive;

    // Opacity = material opacity * albedo texture alpha.
    // u_material[1].z carries the material opacity (albedo.w).
    mediump float opacity = u_material[1].z * albedoSample.w;

    gl_FragColor = vec4(color, opacity);
}
