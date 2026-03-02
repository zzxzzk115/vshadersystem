#pragma vultra language glsl

// Keywords
#pragma keyword permute USE_SHADOW
#pragma keyword special LIGHT_COUNT

#pragma vultra entry vert VSMain
#pragma vultra entry frag FSMain

#pragma vultra material Material

// Parameters
#pragma vultra param baseColorFactor  semantic(BaseColor)  default(1,1,1,1)
#pragma vultra param emissiveFactor  semantic(Emissive)  default(0,0,0,1)
#pragma vultra param metallicFactor  semantic(Metallic)  default(0.0)
#pragma vultra param roughnessFactor semantic(Roughness)  default(0.5)

#pragma vultra param idxBaseColor    semantic(Custom) default(-1)
#pragma vultra param uvScale         semantic(Custom) default(1,1)
#pragma vultra param uvOffset        semantic(Custom) default(0,0)

// Render state
#pragma vultra state Blend One Zero // no blending
#pragma vultra state BlendOp Add Add // just for test
#pragma vultra state ZTest On
#pragma vultra state CompareOp Less
#pragma vultra state ZWrite On
#pragma vultra state Cull Back
#pragma vultra state AlphaToCoverage Off
#pragma vultra state DepthBias 0.1 0.1 // just for test

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout  : require

struct Material
{
    vec4 baseColorFactor;
    vec4 emissiveFactor;

    float metallicFactor;
    float roughnessFactor;

    int idxBaseColor;

    vec2 uvScale;
    vec2 uvOffset;
};


#pragma vultra texture manyTextures semantic(Custom)



layout(set = 1, binding = 0)
uniform sampler2D manyTextures[];


layout(push_constant)
uniform Push
{
    mat4 MVP;
    uint64_t materialAddress;
} pc;


// ============================================================
// Vertex Stage
// ============================================================

[vert]

layout(location = 0)
in vec3 inPos;

layout(location = 1)
in vec2 inUV;


layout(location = 0)
out vec2 outUV;

void VSMain()
{
    gl_Position = pc.MVP * vec4(inPos, 1.0);

    outUV = inUV;
}



// ============================================================
// Fragment Stage
// ============================================================

[frag]

layout(location = 0)
in vec2 inUV;


layout(location = 0)
out vec4 outColor;



vec2 getMaterialUV(Material mat)
{
    return inUV * mat.uvScale + mat.uvOffset;
}


void FSMain()
{
    // GPU-driven material access
    Material mat =
        vshader_LoadMaterial(
            pc.materialAddress);


    vec2 uv =
        getMaterialUV(mat);


    vec4 baseColor =
        texture(
            manyTextures[
                nonuniformEXT(
                    uint(mat.idxBaseColor)
                )
            ],
            uv
        )
        *
        mat.baseColorFactor;


    outColor = baseColor;
}