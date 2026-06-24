// cross_backend: validate that an engine shader is portable by cross-compiling it through
// Slang to every backend's shading language (Vulkan SPIR-V / OpenGL GLSL / WebGPU WGSL /
// D3D12 HLSL / Metal MSL).

#include <vshaderc/slang_compiler.hpp>
#include <vshaderc/slang_validate.hpp>

#include <iostream>

static const char* kShader = R"SLANG(
import vsh;

[VshMaterial]
struct Material { float4 baseColorFactor; float metallicFactor; };

[[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
[[vk::binding(1, 0)]] Texture2D<float4>        uTex;
[[vk::binding(2, 0)]] SamplerState             uSmp;

struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

[shader("vertex")]
V vertexMain(float3 p : POSITION, float2 uv : TEXCOORD0)
{
    V o; o.pos = float4(p, 1); o.uv = uv; return o;
}

[shader("fragment")]
float4 fragmentMain(V i) : SV_Target
{
    return uMat.baseColorFactor * uTex.Sample(uSmp, i.uv) * uMat.metallicFactor;
}
)SLANG";

int main()
{
    vshaderc::SlangCompiler compiler;

    const std::vector<vshaderc::Backend> backends = {
        vshaderc::Backend::Vulkan, vshaderc::Backend::OpenGL, vshaderc::Backend::WebGPU,
        vshaderc::Backend::D3D12,  vshaderc::Backend::Metal,
    };

    vshaderc::SlangCompileOptions opt;
    auto results = vshaderc::validate_backends(compiler, "portable", "portable.slang", kShader, opt, backends);

    std::cout << "Cross-backend validation of a material+texture shader:\n\n";
    bool allOk = true;
    for (const auto& r : results)
    {
        std::cout << "  " << (r.ok ? "[ ok ] " : "[FAIL] ") << vshaderc::backend_name(r.backend);
        if (r.ok)
            std::cout << "   (" << r.outputSize << " bytes generated)";
        else
            std::cout << "   " << r.log.substr(0, 60);
        std::cout << "\n";
        allOk = allOk && r.ok;
    }
    std::cout << "\n" << (allOk ? "Portable across all backends." : "Not portable to every backend.") << "\n";
    return allOk ? 0 : 1;
}
