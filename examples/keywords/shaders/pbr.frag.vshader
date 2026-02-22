#version 460

#pragma keyword permute global USE_SHADOW=1
#pragma keyword permute pass PASS=GBUFFER|FORWARD
#pragma keyword runtime material USE_CLEARCOAT=0
#pragma keyword runtime global DEBUG_VIEW=NONE|NORMAL|ALBEDO
#pragma keyword special USE_BINDLESS=0

layout(set = 0, binding = 0)
uniform sampler2D gAlbedoTex;

layout(push_constant) uniform KeywordPC
{
    uint runtimeMask0;
    uint runtimeMask1;
} KW;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

bool KW_USE_CLEARCOAT()
{
    return (KW.runtimeMask0 & (1 << 0)) != 0;
}

void main()
{
    vec3 albedo = texture(gAlbedoTex, vUV).rgb;

#ifdef USE_SHADOW
    albedo *= 0.8;
#endif

    if (KW_USE_CLEARCOAT())
        albedo = mix(albedo, vec3(1.0), 0.2);

    outColor = vec4(albedo, 1.0);
}
