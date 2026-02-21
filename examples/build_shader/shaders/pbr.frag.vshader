#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout  : require

#include "common.glsl"

// Material marker (Required!)
#pragma vultra material

// ---------------------------
// Material parameters (all configurable)
// ---------------------------

// Core PBR
#pragma vultra param baseColorFactor  semantic(BaseColor)  default(1,1,1,1)
#pragma vultra param metallicFactor   semantic(Metallic)   default(1)          range(0,1)
#pragma vultra param roughnessFactor  semantic(Roughness)  default(1)          range(0,1)
#pragma vultra param occlusionStrength semantic(Occlusion) default(1)          range(0,1)
#pragma vultra param normalScale      semantic(Normal)     default(1)          range(0,2)
#pragma vultra param emissiveFactor   semantic(Emissive)   default(0,0,0,0)

// Alpha controls
#pragma vultra param opacity          semantic(Opacity)    default(1)          range(0,1)
#pragma vultra param alphaCutoff      semantic(AlphaClip)  default(0.5)        range(0,1)

// Workflow/custom knobs
#pragma vultra param clearcoat        semantic(Custom)     default(0)          range(0,1)
#pragma vultra param clearcoatRoughness semantic(Custom)   default(0.1)        range(0,1)
#pragma vultra param specularStrength semantic(Custom)     default(0.5)        range(0,1)

// Bindless texture indices (stored in material; -1 means "no texture")
#pragma vultra param idxBaseColor     semantic(Custom)     default(-1)
#pragma vultra param idxMetallicRoughness semantic(Custom) default(-1)
#pragma vultra param idxNormal        semantic(Custom)     default(-1)
#pragma vultra param idxOcclusion     semantic(Custom)     default(-1)
#pragma vultra param idxEmissive      semantic(Custom)     default(-1)

// UV selection and tiling
#pragma vultra param uvSet            semantic(Custom)     default(0)          range(0,1)
#pragma vultra param uvScale          semantic(Custom)     default(1,1)
#pragma vultra param uvOffset         semantic(Custom)     default(0,0)

// ---------------------------
// Render state
// ---------------------------
#pragma vultra state Blend One Zero // no blending
#pragma vultra state BlendOp Add Add // just for test
#pragma vultra state ZTest On
#pragma vultra state CompareOp Less
#pragma vultra state ZWrite On
#pragma vultra state Cull Back
#pragma vultra state AlphaToCoverage Off
#pragma vultra state DepthBias 0.1 0.1 // just for test

// ---------------------------
// Bindless texture array declaration (semantic only for tooling)
// ---------------------------
#pragma vultra texture manyTextures semantic(Custom)

// ------------------------------------------------------------
// Material block
// Use scalar layout so CPU packing is straightforward (optional).
// ------------------------------------------------------------
layout(set=0, binding=0, scalar) uniform Material
{
    vec4  baseColorFactor;
    vec4  emissiveFactor;

    float metallicFactor;
    float roughnessFactor;

    float occlusionStrength;
    float normalScale;

    float opacity;
    float alphaCutoff;

    float clearcoat;
    float clearcoatRoughness;
    float specularStrength;

    int   idxBaseColor;
    int   idxMetallicRoughness;
    int   idxNormal;
    int   idxOcclusion;
    int   idxEmissive;

    int   uvSet;
    vec2  uvScale;
    vec2  uvOffset;
} mat;

// Bindless array
layout(set=0, binding=2) uniform sampler2D manyTextures[];

// ------------------------------------------------------------
// Inputs (example: a typical forward pass)
// ------------------------------------------------------------
layout(location=0) in vec3 inWorldPos;
layout(location=1) in vec3 inWorldNormal;
layout(location=2) in vec4 inWorldTangent; // xyz = tangent, w = handedness
layout(location=3) in vec2 inUV0;
layout(location=4) in vec2 inUV1;

layout(location=0) out vec4 outColor;

// Push constants (for per-draw, optional)
layout(push_constant) uniform PushConstants
{
    vec3 cameraPos;
    uint _pad0;
} pc;

// ------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------
vec2 select_uv(int uvSet)
{
    return (uvSet == 0) ? inUV0 : inUV1;
}

vec2 material_uv()
{
    return select_uv(mat.uvSet) * mat.uvScale + mat.uvOffset;
}

vec4 sample_bindless_rgba(int idx, vec2 uv, vec4 fallbackValue)
{
    if (idx < 0)
        return fallbackValue;
    return texture(manyTextures[nonuniformEXT(uint(idx))], uv);
}

float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec3  saturate(vec3  x) { return clamp(x, vec3(0.0), vec3(1.0)); }

// GGX helpers (minimal but complete)
float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float d  = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * d * d, 1e-6);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float gv = NoL * sqrt(max(NoV * (NoV - NoV * a2) + a2, 1e-6));
    float gl = NoV * sqrt(max(NoL * (NoL - NoL * a2) + a2, 1e-6));
    return 0.5 / max(gv + gl, 1e-6);
}

vec3 F_Schlick(vec3 F0, float VoH)
{
    float f = pow(1.0 - VoH, 5.0);
    return F0 + (1.0 - F0) * f;
}

// Build TBN and apply normal map
vec3 get_world_normal(vec2 uv, vec3 N, vec4 T4)
{
    vec3 T = normalize(T4.xyz);
    vec3 B = normalize(cross(N, T)) * (T4.w < 0.0 ? -1.0 : 1.0);

    vec3 nrmTS = vec3(0.0, 0.0, 1.0);

    if (mat.idxNormal >= 0)
    {
        vec3 n = texture(manyTextures[nonuniformEXT(uint(mat.idxNormal))], uv).xyz;
        n = n * 2.0 - 1.0;
        n.xy *= mat.normalScale;
        nrmTS = normalize(n);
    }

    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * nrmTS);
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
void main()
{
    vec2 uv = material_uv();

    // Base color (sRGB -> linear)
    vec4 baseColorTex = sample_bindless_rgba(mat.idxBaseColor, uv, vec4(1,1,1,1));
    vec4 baseColor    = mat.baseColorFactor * vec4(toLinear(baseColorTex.rgb), baseColorTex.a);

    // Metallic/Roughness packed (common: B=metallic, G=roughness)
    vec4 mrTex = sample_bindless_rgba(mat.idxMetallicRoughness, uv, vec4(1,1,1,1));
    float metallic  = saturate(mat.metallicFactor  * mrTex.b);
    float roughness = saturate(mat.roughnessFactor * mrTex.g);

    // Occlusion (common: R=occlusion)
    vec4 occTex = sample_bindless_rgba(mat.idxOcclusion, uv, vec4(1,1,1,1));
    float occlusion = mix(1.0, occTex.r, saturate(mat.occlusionStrength));

    // Emissive (sRGB -> linear)
    vec4 emissiveTex = sample_bindless_rgba(mat.idxEmissive, uv, vec4(0,0,0,0));
    vec3 emissive    = mat.emissiveFactor.rgb * toLinear(emissiveTex.rgb);

    // Alpha controls
    float alpha = saturate(baseColor.a * mat.opacity);

    // Alpha clip (optional)
    if (alpha < mat.alphaCutoff)
        discard;

    // Shading frame
    vec3 N = normalize(inWorldNormal);
    N = get_world_normal(uv, N, inWorldTangent);

    vec3 V = normalize(pc.cameraPos - inWorldPos);

    // Example directional light (replace with your light system)
    vec3 L = normalize(vec3(0.4, 0.8, 0.2));
    vec3 H = normalize(V + L);

    float NoV = saturate(dot(N, V));
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));

    // Fresnel base reflectance
    vec3  F0 = mix(vec3(0.04) * mat.specularStrength, baseColor.rgb, metallic);

    // GGX specular
    float a  = max(roughness * roughness, 1e-3);
    float D  = D_GGX(NoH, a);
    float Vis = V_SmithGGXCorrelated(NoV, NoL, a);
    vec3  F  = F_Schlick(F0, VoH);
    vec3  spec = (D * Vis) * F;

    // Lambert diffuse (energy-conserving)
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diff = kd * baseColor.rgb * (1.0 / 3.14159265);

    // Clearcoat (very simple second lobe)
    float cc = saturate(mat.clearcoat);
    float ccR = saturate(mat.clearcoatRoughness);
    float a2 = max(ccR * ccR, 1e-3);
    float D2 = D_GGX(NoH, a2);
    float V2 = V_SmithGGXCorrelated(NoV, NoL, a2);
    vec3  F2 = F_Schlick(vec3(0.04), VoH);
    vec3  clearcoatSpec = cc * (D2 * V2) * F2;

    // Lighting
    vec3 radiance = vec3(5.0); // arbitrary intensity
    vec3 color = (diff + spec + clearcoatSpec) * radiance * NoL;

    // Apply occlusion and add emissive
    color *= occlusion;
    color += emissive;

    // Output (linear -> sRGB)
    outColor = vec4(toGamma(color), alpha);
}