#include <vshadersystem/binary.hpp>
#include <vshadersystem/system.hpp>
#include <vshadersystem/wgsl.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
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
    std::cout << "  descriptors: " << bin.reflection.descriptors.size() << "\n";
    for (const auto& d : bin.reflection.descriptors)
    {
        std::cout << "    - " << d.name << " (set=" << d.set << ", binding=" << d.binding << ", kind="
                  << descriptor_kind_name(d.kind) << ")\n";
    }

    std::cout << "  blocks: " << bin.reflection.blocks.size() << "\n";
    for (const auto& b : bin.reflection.blocks)
    {
        std::cout << "    - " << b.name << " (set=" << b.set << ", binding=" << b.binding << ", size=" << b.size
                  << ")\n";
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
        std::cerr << "[webgpu] compile failed: " << built.error().message << "\n";
        return 1;
    }

    std::cout << "[webgpu] verification ok\n";

    for (const auto& [stage, result] : built.value())
    {
        ShaderBinary bin = result.binary;

        auto wg = spirv_to_wgsl(bin.spirv);
        if (!wg.isOk())
        {
            std::cerr << "[webgpu] SPIR-V -> WGSL failed (" << stage_name(stage) << "): " << wg.error().message
                      << "\n";
            return 2;
        }
        bin.wgsl = std::move(wg.value());

        auto encoded = write_vshbin(bin);
        if (!encoded.isOk())
        {
            std::cerr << "[webgpu] write_vshbin failed (" << stage_name(stage) << "): " << encoded.error().message
                      << "\n";
            return 3;
        }

        auto decoded = read_vshbin(encoded.value());
        if (!decoded.isOk())
        {
            std::cerr << "[webgpu] read_vshbin failed (" << stage_name(stage) << "): " << decoded.error().message
                      << "\n";
            return 4;
        }

        const ShaderBinary& out = decoded.value();
        if (out.wgsl.empty())
        {
            std::cerr << "[webgpu] invalid output (" << stage_name(stage) << "): WGSL is empty\n";
            return 5;
        }

        std::cout << "\n[stage] " << stage_name(out.stage) << "\n";
        std::cout << "  contentHash: " << out.contentHash << "\n";
        std::cout << "  shaderIdHash:" << out.shaderIdHash << "\n";
        std::cout << "  variantHash: " << out.variantHash << "\n";
        std::cout << "  spirvWords:  " << out.spirv.size() << "\n";
        std::cout << "  wgslBytes:   " << out.wgsl.size() << "\n";
        print_reflection(out);

        std::cout << "  wgsl_preview:\n";
        std::cout << out.wgsl.substr(0, std::min<size_t>(out.wgsl.size(), 900)) << "\n";
    }

    return 0;
}
