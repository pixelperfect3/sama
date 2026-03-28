$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos

#include <bgfx_shader.sh>

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
float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Schlick-GGX geometry term for a single direction.
float geometrySchlick(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith geometry function — product of two Schlick-GGX terms.
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return geometrySchlick(max(dot(N, V), 0.0), roughness)
         * geometrySchlick(max(dot(N, L), 0.0), roughness);
}

// Fresnel-Schlick approximation.
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    // -----------------------------------------------------------------------
    // Material inputs
    // -----------------------------------------------------------------------
    vec3 albedo = u_material[0].xyz;
    float roughness = max(u_material[0].w, 0.04);
    float metallic = u_material[1].x;
    float emissiveScale = u_material[1].y;

    // Albedo map (slot 0).  Default white when no texture is bound.
    vec4 albedoSample = texture2D(s_albedo, v_texcoord0);
    albedo *= albedoSample.xyz;

    // ORM map (slot 2): G=roughness, B=metallic.  R is not used — AO comes from s_occlusion.
    vec4 ormSample = texture2D(s_orm, v_texcoord0);
    roughness *= ormSample.y;
    metallic  *= ormSample.z;

    // Occlusion map (slot 4): R=AO.  Defaults to white (ao=1.0) when no texture is bound.
    // Floor at 0.1 to prevent fully-black ambient in the absence of IBL.
    float ao = max(texture2D(s_occlusion, v_texcoord0).x, 0.1);

    // -----------------------------------------------------------------------
    // Normal — TBN normal mapping.
    // -----------------------------------------------------------------------
    vec3 T = normalize(v_tangent);
    vec3 B = normalize(v_bitangent);
    vec3 Ngeom = normalize(v_normal);

    // Sample normal map (slot 1), decode [0,1] -> [-1,1] tangent-space normal.
    vec3 normalSample = texture2D(s_normal, v_texcoord0).xyz;
    normalSample = normalSample * 2.0 - 1.0;

    // Transform tangent-space normal to world space.
    // Expanded manually (T*ns.x + B*ns.y + N*ns.z) to avoid
    // mtxFromCols3 which transposes on Metal's shader path.
    vec3 N = normalize(T * normalSample.x + B * normalSample.y + Ngeom * normalSample.z);

    // -----------------------------------------------------------------------
    // View vector: camera world position from u_frameParams[1].xyz.
    // -----------------------------------------------------------------------
    vec3 camPos = u_frameParams[1].xyz;
    vec3 V = normalize(camPos - v_worldPos);

    // -----------------------------------------------------------------------
    // F0 — dielectric base (0.04) lerped to albedo for metals.
    // -----------------------------------------------------------------------
    vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);

    // -----------------------------------------------------------------------
    // Directional light contribution
    // -----------------------------------------------------------------------
    vec3 L = normalize(u_dirLight[0].xyz);  // points away from surface (toward light)
    vec3 H = normalize(V + L);
    vec3 radiance = u_dirLight[1].xyz;

    // Use normal-mapped N for both diffuse and specular to ensure energy
    // conservation and consistent Cook-Torrance denominator cancellation.
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Diffuse: energy conservation — metals have no diffuse term.
    vec3 kD = (vec3_splat(1.0) - F) * (1.0 - metallic);

    // Cook-Torrance specular BRDF.
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    // Outgoing radiance from the directional light.
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // -----------------------------------------------------------------------
    // Shadow — PCF 2x2, cascade 0 (Phase 4: single directional cascade).
    // Default shadow = 1.0 (fully lit) when:
    //   - u_shadowMatrix[0] is the zero matrix (w = 0 after transform), or
    //   - shadow UV is outside the [0,1]^3 frustum (fragment beyond shadow coverage).
    // Both cases occur in tests and scenes without an active shadow pass.
    // -----------------------------------------------------------------------
    float shadow = 1.0;
    vec4 shadowCoord = mul(u_shadowMatrix[0], vec4(v_worldPos, 1.0));
    if (shadowCoord.w > 0.0001)
    {
        shadowCoord.xyz /= shadowCoord.w;
        if (shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
            shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0 &&
            shadowCoord.z >= 0.0 && shadowCoord.z <= 1.0)
        {
            float texelSize = 1.0 / 2048.0;
            float shadowBias = 0.005;  // prevents self-shadowing (shadow acne)
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
        // Fetch the light index from the flat index texture.
        float idx = texture2D(s_lightIndex,
            vec2((float(lightOffset + li) + 0.5) / 8192.0, 0.5)).x;
        int i = int(idx);

        // Fetch the four rows of light data (view-space position).
        vec4 ld0 = texture2D(s_lightData,
            vec2((float(i) + 0.5) / 256.0, 0.5 / 4.0));
        vec4 ld1 = texture2D(s_lightData,
            vec2((float(i) + 0.5) / 256.0, 1.5 / 4.0));
        vec4 ld2 = texture2D(s_lightData,
            vec2((float(i) + 0.5) / 256.0, 2.5 / 4.0));
        vec4 ld3 = texture2D(s_lightData,
            vec2((float(i) + 0.5) / 256.0, 3.5 / 4.0));

        vec3  lightViewPos = ld0.xyz;
        float lightRad     = ld0.w;
        vec3  lightColor   = ld1.xyz;
        float lightType    = ld1.w;

        // Vector from fragment (view space) to light (view space).
        vec3  Lv   = lightViewPos - v_viewPos;
        float dist = length(Lv);

        if (dist >= lightRad)
            continue;

        vec3 Ln = normalize(Lv);

        // Inverse-square attenuation with smooth quadratic falloff at radius.
        float ratio = dist / lightRad;
        float att = clamp(1.0 - ratio * ratio, 0.0, 1.0);
        att *= att;

        // Spot cone attenuation (type == 1).
        float spotAtt = 1.0;
        if (lightType > 0.5)
        {
            vec3  spotDir  = ld2.xyz;
            float cosOuter = ld2.w;
            float cosInner = ld3.x;
            float cosAngle = dot(-Ln, normalize(spotDir));
            spotAtt = clamp((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.0001),
                            0.0, 1.0);
        }

        // PBR contribution.
        // Ln is in view space; N and V are in world space.  We pass Ln directly
        // into the BRDF functions — this is accurate when camera axes align
        // with world axes (which they do for the approximated V above).
        vec3  H2     = normalize(V + Ln);
        float NdotL2 = max(dot(N, Ln), 0.0);
        float D2     = distributionGGX(N, H2, roughness);
        float G2     = geometrySmith(N, V, Ln, roughness);
        vec3  F2     = fresnelSchlick(max(dot(H2, V), 0.0), F0);
        vec3  kD2    = (vec3_splat(1.0) - F2) * (1.0 - metallic);
        vec3  spec2  = D2 * G2 * F2 / max(4.0 * max(dot(N, V), 0.0) * NdotL2, 0.001);

        Lo += (kD2 * albedo / PI + spec2) * lightColor * att * spotAtt * NdotL2;
    }

    // -----------------------------------------------------------------------
    // Ambient — IBL split-sum (Phase 11).
    // Falls back to flat 0.03 when u_iblParams.y == 0.
    // -----------------------------------------------------------------------
    vec3 ambient;
    if (u_iblParams.y > 0.5)
    {
        // Diffuse IBL: sample irradiance cubemap in the surface normal direction.
        vec3 irradiance = textureCube(s_irradiance, N).rgb;
        vec3 diffuse    = irradiance * albedo * (1.0 - metallic);

        // Specular IBL (split-sum approximation):
        vec3  R                = reflect(-V, N);
        float mipLevel         = roughness * u_iblParams.x;
        vec3  prefilteredColor = textureCubeLod(s_prefiltered, R, mipLevel).rgb;
        vec2  brdf             = texture2D(s_brdfLut, vec2(max(dot(N, V), 0.0), roughness)).xy;
        vec3  specIbl          = prefilteredColor * (F0 * brdf.x + brdf.y);

        ambient = (diffuse + specIbl) * ao;
    }
    else
    {
        // Hemisphere ambient: cool sky from above, warm ground from below.
        // Use the smooth geometry normal (Ngeom) for the hemisphere factor so
        // the ambient varies smoothly with the object shape, not with the
        // high-frequency normal map.  F0-weighted term gives metallic surfaces
        // their colour from indirect light (replaces IBL irradiance/prefilter).
        vec3 skyColor    = vec3(0.90, 0.95, 1.20);
        vec3 groundColor = vec3(0.35, 0.28, 0.18);
        float hemiFactor = Ngeom.y * 0.5 + 0.5;
        vec3 hemi = mix(groundColor, skyColor, hemiFactor);
        ambient = (hemi * albedo * (1.0 - metallic) + hemi * F0) * ao;
    }

    // -----------------------------------------------------------------------
    // Emissive contribution.
    // Sample s_emissive (slot 3); defaults to white when no texture is bound.
    // emissiveScale is 0 for non-emissive materials, so white * 0 = no contribution.
    // -----------------------------------------------------------------------
    vec3 emissive = texture2D(s_emissive, v_texcoord0).rgb * emissiveScale;

    // -----------------------------------------------------------------------
    // Combine, tonemap, and gamma-correct.
    // Inline Reinhard tonemap — Phase 7 will replace this with a full
    // post-process pass (ACES or Uncharted2 from RenderSettings).
    // -----------------------------------------------------------------------
    vec3 color = ambient + Lo + emissive;

    // Reinhard
    color = color / (color + vec3_splat(1.0));

    // Gamma correction (sRGB, gamma = 2.2)
    color = pow(color, vec3_splat(1.0 / 2.2));

    gl_FragColor = vec4(color, 1.0);
}
