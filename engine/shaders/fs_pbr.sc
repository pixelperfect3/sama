$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0

#include <bgfx_shader.sh>

// [0] = {albedo.rgb, roughness}   [1] = {metallic, emissiveScale, 0, 0}
uniform vec4 u_material[2];

// [0] = {dir.xyz, 0}   [1] = {color.rgb * intensity, 0}
// dir points from the surface toward the light source (normalised, away from surface).
uniform vec4 u_dirLight[2];

// [0] = {viewportWidth, viewportHeight, near, far}   [1] = {time, dt, 0, 0}
uniform vec4 u_frameParams[2];

SAMPLER2D(s_albedo, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_orm, 2);

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

    // ORM map (slot 2): R=occlusion, G=roughness, B=metallic.
    vec4 ormSample = texture2D(s_orm, v_texcoord0);
    float ao = ormSample.x;
    roughness *= ormSample.y;
    metallic *= ormSample.z;

    // -----------------------------------------------------------------------
    // Normal — Phase 3 passthrough.
    // Normal map (slot 1) is sampled but not yet applied to the TBN frame.
    // Phase 3 uses the interpolated geometry normal directly.
    // Full TBN normal mapping will be wired up once the sampler is exercised.
    // -----------------------------------------------------------------------
    vec3 N = normalize(v_normal);

    // View vector: approximate eye at world origin for Phase 3
    // (accurate when the view-space transform places the camera at origin).
    vec3 V = normalize(-v_worldPos);

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
    // Ambient — flat constant, no IBL (Phase 11 will replace this).
    // -----------------------------------------------------------------------
    vec3 ambient = vec3_splat(0.03) * albedo * ao;

    // -----------------------------------------------------------------------
    // Emissive contribution.
    // -----------------------------------------------------------------------
    vec3 emissive = albedo * emissiveScale;

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
