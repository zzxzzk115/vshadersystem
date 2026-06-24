// A representative engine PBR shader. The vsh attributes describe everything a material
// editor needs to draw an inspector, and everything an engine needs to build a pipeline.
import vsh;

[VshMaterial]
struct Material
{
    [VshSemantic("baseColor")] [VshColor] [VshDefault("1,1,1,1")] [VshDisplayName("Base Color")]
    float4 baseColorFactor;

    [VshSemantic("metallic")] [VshRange(0, 1)] [VshDefault("0.0")] [VshDisplayName("Metallic")]
    float metallicFactor;

    [VshSemantic("roughness")] [VshRange(0, 1)] [VshDefault("0.5")] [VshDisplayName("Roughness")]
    float roughnessFactor;

    [VshSemantic("emissive")] [VshColor] [VshDefault("0,0,0,1")] [VshDisplayName("Emissive")]
    float4 emissiveFactor;

    [VshTexture("Texture2D")] [VshDisplayName("Base Color Map")]
    int baseColorTex_index;

    [VshTexture("Texture2D")] [VshDisplayName("Normal Map")]
    int normalTex_index;
};

[VshKeyword("USE_NORMAL_MAP", "bool", "permute", "material")]
[VshKeyword("SHADING", "enum:unlit,lambert,pbr", "permute", "global")]
[VshRenderState("cull", "back")]
[VshRenderState("depth_test", "on")]
[VshRenderState("depth_write", "on")]
[VshRenderState("blend", "off")]
void __vsh_meta() {}

[[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
[[vk::binding(1, 0)]] Texture2D<float4>        uTextures[256]; // bindless atlas
[[vk::binding(2, 0)]] SamplerState             uSampler;

struct VSIn  { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };

[shader("vertex")]
VSOut vertexMain(VSIn i)
{
    VSOut o;
    o.pos = float4(i.position, 1);
    o.nrm = i.normal;
    o.uv  = i.uv;
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOut i) : SV_Target
{
    float4 base = uMat.baseColorFactor * uTextures[uMat.baseColorTex_index].Sample(uSampler, i.uv);
    float3 n = i.nrm;
#if USE_NORMAL_MAP
    n = normalize(uTextures[uMat.normalTex_index].Sample(uSampler, i.uv).xyz * 2 - 1);
#endif
    float3 color = base.rgb;
#if SHADING == 1
    color *= saturate(dot(n, float3(0, 0, 1)));
#elif SHADING == 2
    float ndl = saturate(dot(n, float3(0, 0, 1)));
    color *= lerp(uMat.metallicFactor, 1.0, ndl) * (1 - uMat.roughnessFactor * 0.5);
#endif
    return float4(color + uMat.emissiveFactor.rgb, base.a);
}
