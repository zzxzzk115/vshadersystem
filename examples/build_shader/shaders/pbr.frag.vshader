#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "common.glsl"

#pragma vultra material
#pragma vultra param baseColor semantic(BaseColor) default(1,1,1,1)
#pragma vultra param metallic semantic(Metallic) default(0) range(0,1)
#pragma vultra texture baseColorTex semantic(BaseColor)
#pragma vultra texture manyTextures semantic(Custom)
#pragma vultra blend off
#pragma vultra depthTest on
#pragma vultra depthWrite on
#pragma vultra cull back

layout(set=0, binding=0) uniform Material
{
    vec4 baseColor;
    float metallic;
};

layout(set=0, binding=1) uniform sampler2D baseColorTex;

// Test bindless
layout(set=0, binding=2) uniform sampler2D manyTextures[];

layout(push_constant) uniform PushConstants
{
    uint testTextureIndex;
} pc;

layout(location=0) out vec4 outColor;

void main()
{
    vec4 baseColor = texture(baseColorTex, vec2(0.5, 0.5));
    vec4 testBindless = texture(manyTextures[nonuniformEXT(pc.testTextureIndex)], vec2(0.5, 0.5));
    vec3 c = applyGamma(testBindless.rgb);
    outColor = vec4(c, baseColor.a);
}
