[vshader]
language = glsl
version = 460

[keywords]
USE_SHADOW : bool permute
QUALITY    : enum(low,medium,high) permute

[properties]
baseColorFactor : vec4 = (1,1,1,1)
metallicFactor  : float = 0.0
roughnessFactor : float = 0.5
baseColorTex    : Texture2D
normalTex       : Texture2D

[renderstate]
cull = back
depth_test = on
depth_write = on
depth_func = less
blend = off


[vertex]
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 outUV;

layout(push_constant)
uniform Push
{
    mat4 MVP;
} pc;

void main()
{
    gl_Position = pc.MVP * vec4(inPos, 1.0);
    outUV = inUV;
}


[fragment]
#include "common.glsl"

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout  : require

layout(location = 0) in vec2 outUV;
layout(location = 0) out vec4 outColor;

vec3 applyQuality(vec3 c)
{
#if QUALITY == 0
    return c * 0.5;
#elif QUALITY == 1
    return c;
#else
    return pow(c, vec3(1.2));
#endif
}

void main()
{
    // auto-injected by vshadersystem:
    Material mat = VSH_MATERIAL();

    vec4 baseColor =
        VSH_SAMPLE2D(mat.baseColorTex_index, outUV)
        * mat.baseColorFactor;

#ifdef USE_SHADOW
    baseColor.rgb *= 0.7;
#endif

    baseColor.rgb = applyQuality(baseColor.rgb);

    outColor = vec4(baseColor.rgb, 1.0);
}