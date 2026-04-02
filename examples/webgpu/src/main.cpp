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

int main()
{
    // Minimal INI-style shader source used for WebGPU translation validation.
    const std::string shaderSrc = R"([vshader]
language = glsl
version = 450

[fragment]
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(0.2, 0.6, 0.9, 1.0);
}
)";

    BuildRequest req;
    req.source.virtualPath             = "webgpu_demo.frag.vshader";
    req.source.sourceText              = shaderSrc;
    req.options.stage                  = ShaderStage::eFrag;
    req.options.language               = ShaderLanguage::eGLSL;
    req.options.materialAccessMode     = MaterialAccessMode::eSSBO; // WebGPU-safe mode (no BDA).
    req.options.dumpSourceOnError      = true;
    req.options.dumpContextLines       = 5;
    req.options.emitIntermediateAlways = false;

    auto built = build_single_shader(req);
    if (!built.isOk())
    {
        std::cerr << "[webgpu] compile failed: " << built.error().message << "\n";
        return 1;
    }

    ShaderBinary bin = built.value().binary;

    auto wg = spirv_to_wgsl(bin.spirv);
    if (!wg.isOk())
    {
        std::cerr << "[webgpu] SPIR-V -> WGSL failed: " << wg.error().message << "\n";
        return 2;
    }
    bin.wgsl = std::move(wg.value());

    auto encoded = write_vshbin(bin);
    if (!encoded.isOk())
    {
        std::cerr << "[webgpu] write_vshbin failed: " << encoded.error().message << "\n";
        return 3;
    }

    auto decoded = read_vshbin(encoded.value());
    if (!decoded.isOk())
    {
        std::cerr << "[webgpu] read_vshbin failed: " << decoded.error().message << "\n";
        return 4;
    }

    const ShaderBinary& out = decoded.value();

    std::cout << "[webgpu] verification ok\n";
    std::cout << "  stage:       " << stage_name(out.stage) << "\n";
    std::cout << "  contentHash: " << out.contentHash << "\n";
    std::cout << "  shaderIdHash:" << out.shaderIdHash << "\n";
    std::cout << "  variantHash: " << out.variantHash << "\n";
    std::cout << "  spirvWords:  " << out.spirv.size() << "\n";
    std::cout << "  wgslBytes:   " << out.wgsl.size() << "\n";

    std::cout << "  wgsl:\n";
    std::cout << out.wgsl << "\n";

    if (out.wgsl.empty())
    {
        std::cerr << "[webgpu] invalid output: WGSL is empty\n";
        return 5;
    }

    return 0;
}
