#pragma once

#include "vshadersystem/keywords.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vshadersystem
{
    // ------------------------------------------------------------
    // Shader stage
    // ------------------------------------------------------------
    // NOTE: this value is serialized as a uint8 into the .vshlib variant key, so new
    // enumerators must be APPENDED - inserting one mid-enum invalidates every library
    // cooked by an older build.
    enum class ShaderStage : uint8_t
    {
        eUnknown = 0,
        eVert,
        eFrag,
        eGeom,
        eComp,
        eTask,
        eMesh,
        eRgen,
        eRmiss,
        eRchit,
        eRahit,
        eRint,
        eHull,   // tessellation control (HLSL hull / GLSL .tesc)
        eDomain, // tessellation evaluation (HLSL domain / GLSL .tese)
    };

    using ShaderStageFlags = uint32_t;

    enum ShaderStageFlagBits : ShaderStageFlags
    {
        eStateUnknown = 0,
        eStageVert    = 1 << 0,
        eStageFrag    = 1 << 1,
        eStageGeom    = 1 << 2,
        eStageComp    = 1 << 3,
        eStageTask    = 1 << 4,
        eStageMesh    = 1 << 5,
        eStageRgen    = 1 << 6,
        eStageRmiss   = 1 << 7,
        eStageRchit   = 1 << 8,
        eStageRahit   = 1 << 9,
        eStageRint    = 1 << 10,
        eStageHull    = 1 << 11,
        eStageDomain  = 1 << 12
    };

    // ------------------------------------------------------------
    // Shader language
    // ------------------------------------------------------------
    enum class ShaderLanguage : uint8_t
    {
        eAuto = 0,
        eGLSL
    };

    // ------------------------------------------------------------
    // Material access mode (tool-layer injected API)
    // ------------------------------------------------------------
    enum class MaterialAccessMode : uint8_t
    {
        eBDA = 0,     // Buffer Device Address / PhysicalStorageBuffer
        eUBO,         // Uniform Buffer (std140)
        eSSBO,        // Storage Buffer (std430) + index
        ePushConstant // Push constant
    };

    // ------------------------------------------------------------
    // Descriptor kinds
    // ------------------------------------------------------------
    enum class DescriptorKind : uint8_t
    {
        eUniformBuffer = 0,
        eStorageBuffer,
        eSampledImage,
        eStorageImage,
        eSampler,
        eCombinedImageSampler,
        eAccelerationStructure,
        eUnknown
    };

    // ------------------------------------------------------------
    // Resource access
    // ------------------------------------------------------------
    enum class ResourceAccess : uint8_t
    {
        eUnknown = 0,
        eReadOnly,
        eWriteOnly,
        eReadWrite
    };

    // ------------------------------------------------------------
    // Semantic
    // ------------------------------------------------------------
    enum class Semantic : uint16_t
    {
        eUnknown = 0,

        eBaseColor,
        eMetallic,
        eRoughness,
        eNormal,
        eEmissive,
        eOcclusion,

        eOpacity,
        eAlphaClip,

        eCustom
    };

    // ------------------------------------------------------------
    // Parameter types
    // ------------------------------------------------------------
    enum class ParamType : uint8_t
    {
        eFloat = 0,
        eVec2,
        eVec3,
        eVec4,
        eInt,
        eUInt,
        eBool,
        eMat3,
        eMat4
    };

    // ------------------------------------------------------------
    // Texture types
    // ------------------------------------------------------------
    enum class TextureType : uint8_t
    {
        eTex2D = 0,
        eTexCube,
        eTex3D,
        eTex2DArray,
        eUnknown
    };

    // ------------------------------------------------------------
    // Render state
    // ------------------------------------------------------------

    // ------------------------------------------------------------
    // Depth compare op (ZTest)
    // ------------------------------------------------------------
    enum class CompareOp : uint8_t
    {
        eNever = 0,
        eLess,
        eEqual,
        eLessOrEqual,
        eGreater,
        eNotEqual,
        eGreaterOrEqual,
        eAlways
    };

    // ------------------------------------------------------------
    // Blend factors
    // ------------------------------------------------------------
    enum class BlendFactor : uint8_t
    {
        eZero = 0,
        eOne,

        eSrcColor,
        eOneMinusSrcColor,

        eDstColor,
        eOneMinusDstColor,

        eSrcAlpha,
        eOneMinusSrcAlpha,

        eDstAlpha,
        eOneMinusDstAlpha
    };

    // ------------------------------------------------------------
    // Blend operations
    // ------------------------------------------------------------
    enum class BlendOp : uint8_t
    {
        eAdd = 0,
        eSubtract,
        eReverseSubtract,
        eMin,
        eMax
    };

    // ------------------------------------------------------------
    // Color mask flags
    // ------------------------------------------------------------
    using ColorMaskFlags = uint8_t;

    enum ColorMaskFlagBits : ColorMaskFlags
    {
        eColorMaskNone = 0,

        eColorMaskR = 1 << 0,
        eColorMaskG = 1 << 1,
        eColorMaskB = 1 << 2,
        eColorMaskA = 1 << 3,

        eColorMaskRGB  = eColorMaskR | eColorMaskG | eColorMaskB,
        eColorMaskRGBA = eColorMaskR | eColorMaskG | eColorMaskB | eColorMaskA
    };

    // ------------------------------------------------------------
    // Cull mode
    // ------------------------------------------------------------
    enum class CullMode : uint8_t
    {
        eNone = 0,
        eBack,
        eFront
    };

    // ------------------------------------------------------------
    // RenderState)
    // ------------------------------------------------------------
    struct RenderState
    {
        // --------------------------------------------------------
        // Depth
        // --------------------------------------------------------
        bool      depthTest  = true;
        bool      depthWrite = true;
        CompareOp depthFunc  = CompareOp::eLessOrEqual;

        // --------------------------------------------------------
        // Raster
        // --------------------------------------------------------
        CullMode cull = CullMode::eBack;

        // --------------------------------------------------------
        // Blend
        // --------------------------------------------------------
        bool blendEnable = false;

        BlendFactor srcColor = BlendFactor::eOne;
        BlendFactor dstColor = BlendFactor::eZero;
        BlendOp     colorOp  = BlendOp::eAdd;

        BlendFactor srcAlpha = BlendFactor::eOne;
        BlendFactor dstAlpha = BlendFactor::eZero;
        BlendOp     alphaOp  = BlendOp::eAdd;

        // --------------------------------------------------------
        // Color mask
        // --------------------------------------------------------
        ColorMaskFlags colorMask = eColorMaskRGBA;

        // --------------------------------------------------------
        // Alpha to coverage
        // --------------------------------------------------------
        bool alphaToCoverage = false;

        // --------------------------------------------------------
        // Depth bias
        // --------------------------------------------------------
        float depthBiasFactor = 0.0f;
        float depthBiasUnits  = 0.0f;
    };

    // ------------------------------------------------------------
    // Reflection structures
    // ------------------------------------------------------------
    struct DescriptorBinding
    {
        std::string name;
        uint32_t    set     = 0;
        uint32_t    binding = 0;
        uint32_t    count   = 1;

        DescriptorKind kind = DescriptorKind::eUnknown;

        ResourceAccess access = ResourceAccess::eUnknown;

        ShaderStageFlags stageFlags = 0;

        bool runtimeSized = false;

        // Texture view dimension for image descriptors (2D/Cube/3D/2DArray). Needed by backends that
        // must declare the dimension in the bind-group layout (e.g. WebGPU samplerCube). eUnknown for
        // non-image descriptors.
        TextureType textureType = TextureType::eUnknown;
    };

    struct BlockMember
    {
        std::string name;
        uint32_t    offset = 0;
        uint32_t    size   = 0;
        ParamType   type   = ParamType::eFloat;
    };

    struct BlockLayout
    {
        std::string name;

        uint32_t set     = 0;
        uint32_t binding = 0;

        uint32_t size = 0;

        bool isPushConstant = false;

        ResourceAccess access = ResourceAccess::eUnknown;

        ShaderStageFlags stageFlags = 0;

        std::vector<BlockMember> members;
    };

    // Vertex stage input attribute (for building vertex input state / input layout).
    struct VertexInput
    {
        std::string name;
        std::string semantic; // HLSL-style semantic, e.g. "POSITION", "TEXCOORD0"
        uint32_t    location = 0;
        ParamType   type     = ParamType::eFloat;
    };

    // Fragment stage color output (SV_Target N) for color-attachment configuration.
    struct FragmentOutput
    {
        std::string name;
        uint32_t    location = 0;
        ParamType   type     = ParamType::eVec4;
    };

    struct ShaderReflection
    {
        std::vector<DescriptorBinding> descriptors;
        std::vector<BlockLayout>       blocks;

        // Graphics stage I/O (empty for compute / non-matching stages).
        std::vector<VertexInput>    vertexInputs;
        std::vector<FragmentOutput> colorOutputs;

        bool hasLocalSize = false;

        uint32_t localSizeX = 1;
        uint32_t localSizeY = 1;
        uint32_t localSizeZ = 1;
    };

    // ------------------------------------------------------------
    // Material description
    // ------------------------------------------------------------
    struct ParamRange
    {
        double min = 0;
        double max = 1;
    };

    struct ParamDefault
    {
        ParamType type            = ParamType::eFloat;
        uint8_t   valueBuffer[64] = {}; // enough for mat4
    };

    struct MaterialParamDesc
    {
        struct EnumOption
        {
            std::string label;
            int32_t     value = 0;
        };

        std::string name;

        ParamType type = ParamType::eFloat;

        uint32_t offset = 0;
        uint32_t size   = 0;

        Semantic semantic = Semantic::eUnknown;

        bool         hasDefault = false;
        ParamDefault defaultValue;

        bool       hasRange = false;
        ParamRange range;

        std::vector<EnumOption> enumOptions;

        // Editor hints.
        bool        isColor = false; // draw a color picker (from [VshColor])
        std::string displayName;     // friendly label (from [VshDisplayName]); empty => use name
    };

    struct MaterialTextureDesc
    {
        std::string name;

        TextureType type = TextureType::eUnknown;

        uint32_t set     = 0;
        uint32_t binding = 0;
        uint32_t count   = 1;

        Semantic semantic = Semantic::eUnknown;
    };

    struct MaterialDescription
    {
        std::string materialBlockName = "Material";

        uint32_t materialParamSize = 0;

        std::vector<MaterialParamDesc>   params;
        std::vector<MaterialTextureDesc> textures;

        RenderState renderState;
    };

    // ------------------------------------------------------------
    // Shader binary
    // ------------------------------------------------------------
    struct ShaderBinary
    {
        uint64_t contentHash  = 0;
        uint64_t shaderIdHash = 0; // stable logical shader id hash for runtime lookup
        // Hash of the resolved permutation keyword set for this compiled binary.
        // Used as the primary lookup key inside a .vshlib.
        // 0 means "not computed" (older files).
        uint64_t variantHash = 0;
        uint64_t spirvHash   = 0;

        ShaderStage stage = ShaderStage::eFrag;

        // Entry point function name (e.g. "vertexMain"); used as the pipeline pName.
        std::string entryPointName;

        ShaderReflection reflection;

        MaterialDescription materialDesc;

        // The shader's keyword declarations (same across all of its variants), so an
        // editor/runtime can enumerate toggleable features from a loaded binary.
        std::vector<KeywordDecl> keywords;

        std::vector<uint32_t> spirv; // Vulkan (and OpenGL/Metal via transpilers)
        std::string           wgsl;  // WebGPU
        std::vector<uint8_t>  dxbc;  // Direct3D 12 (Shader Model 5.1, via fxc)
        std::vector<uint8_t>  dxil;  // Direct3D 12 (Shader Model 6.0+, via dxc)
    };
} // namespace vshadersystem
