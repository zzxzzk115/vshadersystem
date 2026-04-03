#include <vshadersystem/binary.hpp>
#include <vshadersystem/log.hpp>
#include <vshadersystem/system.hpp>
#include <vshadersystem/wgsl.hpp>

#include <algorithm>
#include <cstdint>
#include <string>

using namespace vshadersystem;

static const char* stage_name(ShaderStage s)
{
    switch (s)
    {
        case ShaderStage::eVert:
            return "vert";
        case ShaderStage::eFrag:
            return "frag";
        case ShaderStage::eComp:
            return "comp";
        default:
            return "unknown";
    }
}

static const char* descriptor_kind_name(DescriptorKind kind)
{
    switch (kind)
    {
        case DescriptorKind::eUniformBuffer:
            return "uniform-buffer";
        case DescriptorKind::eStorageBuffer:
            return "storage-buffer";
        case DescriptorKind::eSampledImage:
            return "sampled-image";
        case DescriptorKind::eStorageImage:
            return "storage-image";
        case DescriptorKind::eSampler:
            return "sampler";
        case DescriptorKind::eCombinedImageSampler:
            return "combined-image-sampler";
        case DescriptorKind::eAccelerationStructure:
            return "accel-struct";
        default:
            return "unknown";
    }
}

static void print_reflection(const ShaderBinary& bin)
{
    VSS_LOG_TAG_INFO("webgpu", "  descriptors: %zu", bin.reflection.descriptors.size());
    for (const auto& d : bin.reflection.descriptors)
    {
        VSS_LOG_TAG_INFO("webgpu",
                         "    - %s (set=%u, binding=%u, kind=%s)",
                         d.name.c_str(),
                         d.set,
                         d.binding,
                         descriptor_kind_name(d.kind));
    }

    VSS_LOG_TAG_INFO("webgpu", "  blocks: %zu", bin.reflection.blocks.size());
    for (const auto& b : bin.reflection.blocks)
    {
        VSS_LOG_TAG_INFO("webgpu", "    - %s (set=%u, binding=%u, size=%u)", b.name.c_str(), b.set, b.binding, b.size);
    }
}

int main()
{
    // A richer INI-style shader for WebGPU path validation.
    // Includes: UBO usage + texture sampling + vertex/fragment pipeline stages.
    const std::string shaderSrc = R"([vshader]
language = glsl
version = 450

[properties]
baseColorFactor : vec4 = (1.0, 1.0, 1.0, 1.0)
uvScale         : vec2 = (1.0, 1.0)

[vertex]
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;

layout(set = 0, binding = 0, std140) uniform FrameUBO
{
    mat4 uViewProj;
    vec4 uTint;
    vec4 uFrameParam; // x=time, y=mix, z/w reserved
} uFrame;

void main()
{
    gl_Position = uFrame.uViewProj * vec4(inPos, 1.0);
    vUV = inUV;
}

[fragment]
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform FrameUBO
{
    mat4 uViewProj;
    vec4 uTint;
    vec4 uFrameParam; // x=time, y=mix, z/w reserved
} uFrame;

layout(set = 1, binding = 0) uniform texture2D uBaseColorTex;
layout(set = 1, binding = 1) uniform texture2D uDetailTex;
layout(set = 1, binding = 2) uniform sampler uLinearSampler;

void main()
{
    float timeValue = uFrame.uFrameParam.x;
    float mixValue = clamp(uFrame.uFrameParam.y, 0.0, 1.0);

    vec2 uv0 = vUV;
    vec2 uv1 = vUV * 4.0 + vec2(timeValue * 0.15, 0.0);

    vec4 baseColor = texture(sampler2D(uBaseColorTex, uLinearSampler), uv0);
    vec4 detailColor = texture(sampler2D(uDetailTex, uLinearSampler), uv1);

    vec4 layered = mix(baseColor, detailColor, mixValue);
    outColor = layered * uFrame.uTint;
}
)";

    BuildRequest req;
    req.source.virtualPath             = "webgpu_rich_demo.vshader";
    req.source.sourceText              = shaderSrc;
    req.options.language               = ShaderLanguage::eGLSL;
    req.options.materialAccessMode     = MaterialAccessMode::eUBO; // WebGPU-safe mode.
    req.options.webgpuProfile          = true;
    req.options.dumpSourceOnError      = true;
    req.options.dumpContextLines       = 5;
    req.options.emitIntermediateAlways = false;

    auto built = build_multiple_shaders(req);
    if (!built.isOk())
    {
        VSS_LOG_TAG_ERROR("webgpu", "compile failed: %s", built.error().message.c_str());
        return 1;
    }

    VSS_LOG_TAG_INFO("webgpu", "verification ok");

    for (const auto& [stage, result] : built.value())
    {
        ShaderBinary bin = result.binary;

        auto wg = spirv_to_wgsl(bin.spirv);
        if (!wg.isOk())
        {
            VSS_LOG_TAG_ERROR(
                "webgpu", "SPIR-V -> WGSL failed (%s): %s", stage_name(stage), wg.error().message.c_str());
            return 2;
        }
        bin.wgsl = std::move(wg.value());

        auto encoded = write_vshbin(bin);
        if (!encoded.isOk())
        {
            VSS_LOG_TAG_ERROR(
                "webgpu", "write_vshbin failed (%s): %s", stage_name(stage), encoded.error().message.c_str());
            return 3;
        }

        auto decoded = read_vshbin(encoded.value());
        if (!decoded.isOk())
        {
            VSS_LOG_TAG_ERROR(
                "webgpu", "read_vshbin failed (%s): %s", stage_name(stage), decoded.error().message.c_str());
            return 4;
        }

        const ShaderBinary& out = decoded.value();
        if (out.wgsl.empty())
        {
            VSS_LOG_TAG_ERROR("webgpu", "invalid output (%s): WGSL is empty", stage_name(stage));
            return 5;
        }

        VSS_LOG_TAG_INFO("webgpu", "[stage] %s", stage_name(out.stage));
        VSS_LOG_TAG_INFO("webgpu", "  contentHash: %llu", static_cast<unsigned long long>(out.contentHash));
        VSS_LOG_TAG_INFO("webgpu", "  shaderIdHash:%llu", static_cast<unsigned long long>(out.shaderIdHash));
        VSS_LOG_TAG_INFO("webgpu", "  variantHash: %llu", static_cast<unsigned long long>(out.variantHash));
        VSS_LOG_TAG_INFO("webgpu", "  spirvWords:  %zu", out.spirv.size());
        VSS_LOG_TAG_INFO("webgpu", "  wgslBytes:   %zu", out.wgsl.size());
        print_reflection(out);

        VSS_LOG_TAG_INFO("webgpu", "  wgsl_preview:");
        const std::string preview = out.wgsl.substr(0, std::min<size_t>(out.wgsl.size(), 900));
        VSS_LOG_TAG_INFO("webgpu", "%s", preview.c_str());
    }

    return 0;
}
