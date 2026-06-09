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
toneMode        : enum(linear=0,aces=1,reinhard=2) = aces
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

    baseColor.rgb = applyQuality(baseColor.rgb + float(mat.toneMode) * 0.0);

    outColor = vec4(baseColor.rgb, 1.0);
}

[geometry]
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec2 inUV[];
layout(location = 0) out vec2 outUV;

void main()
{
    for (int i = 0; i < 3; ++i)
    {
        outUV       = inUV[i];
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}

[compute]
#extension GL_EXT_scalar_block_layout : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 2, binding = 0, std430) readonly buffer ReadOnlyData
{
    vec4 values[];
} g_ReadOnlyData;

layout(set = 2, binding = 1, std430) writeonly buffer WriteOnlyData
{
    vec4 values[];
} g_WriteOnlyData;

layout(set = 2, binding = 2, std430) buffer ReadWriteData
{
    vec4 values[];
} g_ReadWriteData;

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    vec4 v = g_ReadOnlyData.values[idx];
    g_ReadWriteData.values[idx] = v;
    g_WriteOnlyData.values[idx] = v * vec4(2.0, 2.0, 2.0, 1.0);
}
