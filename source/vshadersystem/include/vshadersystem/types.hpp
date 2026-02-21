#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vshadersystem
{
    // ------------------------------------------------------------
    // Shader stage
    // ------------------------------------------------------------
    enum class ShaderStage : uint8_t
    {
        eVert = 0,
        eFrag,
        eComp,
        eTask,
        eMesh,
        eRgen,
        eRmiss,
        eRchit,
        eRahit,
        eRint
    };

    using ShaderStageFlags = uint32_t;

    enum ShaderStageFlagBits : ShaderStageFlags
    {
        eStageVert  = 1 << 0,
        eStageFrag  = 1 << 1,
        eStageComp  = 1 << 2,
        eStageTask  = 1 << 3,
        eStageMesh  = 1 << 4,
        eStageRgen  = 1 << 5,
        eStageRmiss = 1 << 6,
        eStageRchit = 1 << 7,
        eStageRahit = 1 << 8,
        eStageRint  = 1 << 9
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
    enum class BlendMode : uint8_t
    {
        eOff = 0,
        eAlpha,
        eAdd
    };

    enum class CullMode : uint8_t
    {
        eNone = 0,
        eBack,
        eFront
    };

    struct RenderStateHints
    {
        bool      depthTest  = true;
        bool      depthWrite = true;
        BlendMode blend      = BlendMode::eOff;
        CullMode  cull       = CullMode::eBack;
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

        ShaderStageFlags stageFlags = 0;

        bool runtimeSized = false;
    };

    struct BlockMember
    {
        std::string name;
        uint32_t    offset = 0;
        uint32_t    size   = 0;
    };

    struct BlockLayout
    {
        std::string name;

        uint32_t set     = 0;
        uint32_t binding = 0;

        uint32_t size = 0;

        bool isPushConstant = false;

        ShaderStageFlags stageFlags = 0;

        std::vector<BlockMember> members;
    };

    struct ShaderReflection
    {
        std::vector<DescriptorBinding> descriptors;
        std::vector<BlockLayout>       blocks;

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
        ParamType type  = ParamType::eFloat;
        double    v[16] = {};
    };

    struct MaterialParamDesc
    {
        std::string name;

        ParamType type = ParamType::eFloat;

        uint32_t offset = 0;
        uint32_t size   = 0;

        Semantic semantic = Semantic::eUnknown;

        bool         hasDefault = false;
        ParamDefault defaultValue;

        bool       hasRange = false;
        ParamRange range;
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

        RenderStateHints renderState;
    };

    // ------------------------------------------------------------
    // Shader binary
    // ------------------------------------------------------------
    struct ShaderBinary
    {
        uint64_t contentHash = 0;
        uint64_t spirvHash   = 0;

        ShaderStage stage = ShaderStage::eFrag;

        ShaderReflection reflection;

        MaterialDescription materialDesc;

        std::vector<uint32_t> spirv;
    };
} // namespace vshadersystem