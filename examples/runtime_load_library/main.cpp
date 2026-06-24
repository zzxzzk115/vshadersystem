// vshadersystem v1.0 example: author Slang, build a variant library, then load it back
// the way an engine would (runtime reader, zero Slang) and inspect reflection + material.

#include <vshaderc/slang_build.hpp>
#include <vshaderc/slang_compiler.hpp>

#include <vshadersystem/variant_key.hpp>
#include <vshadersystem/vsh_format.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using namespace vshadersystem;

static const char* kShader = R"SLANG(
import vsh;

[VshMaterial]
struct Material
{
    [VshSemantic("baseColor")]                 float4 baseColorFactor;
    [VshSemantic("roughness")][VshRange(0, 1)] float  roughnessFactor;
};

[VshKeyword("USE_SHADOW", "bool", "permute", "global")]
[VshRenderState("cull", "back")]
void __vsh_meta() {}

[[vk::binding(0, 0)]] ConstantBuffer<Material>     uMat;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4>   uOut;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    float3 c = uMat.baseColorFactor.rgb * (1.0 - uMat.roughnessFactor);
#if USE_SHADOW
    c *= 0.8;
#endif
    uOut[tid.x] = float4(c, uMat.baseColorFactor.a);
}
)SLANG";

static const char* kind_name(DescriptorKind k)
{
    switch (k)
    {
        case DescriptorKind::eUniformBuffer: return "uniform-buffer";
        case DescriptorKind::eStorageBuffer: return "storage-buffer";
        case DescriptorKind::eSampledImage: return "sampled-image";
        case DescriptorKind::eStorageImage: return "storage-image";
        case DescriptorKind::eSampler: return "sampler";
        case DescriptorKind::eAccelerationStructure: return "accel-struct";
        default: return "unknown";
    }
}

int main()
{
    namespace v1 = vshadersystem::v1;

    // ----- offline: compile + expand variants (this is the vshaderc side) -----
    vshaderc::SlangCompiler compiler;
    if (!compiler.isValid())
    {
        std::cerr << "failed to init Slang\n";
        return 1;
    }

    vshaderc::ShaderBuildOptions bo;
    bo.shaderId         = "example/material_compute";
    bo.compile.emitWgsl = true;

    auto br = vshaderc::build_shader(compiler, "material_compute", "material_compute.slang", kShader, bo);
    if (!br.isOk())
    {
        std::cerr << "build failed: " << br.error().message << "\n";
        return 1;
    }
    const auto& bres = br.value();

    std::vector<v1::LibraryEntry> entries;
    for (const auto& v : bres.variants)
    {
        auto bin = v1::write_binary(vshaderc::to_shader_binary(v, bres.shaderIdHash));
        entries.push_back({v.variantHash, v.stage, bin.value()});
    }

    const std::string libPath = "shaders/material_compute.vshlib";
    std::filesystem::create_directories(std::filesystem::path(libPath).parent_path());
    auto libBytes = v1::write_library(entries, nullptr);
    {
        std::ofstream f(libPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(libBytes.value().data()),
                static_cast<std::streamsize>(libBytes.value().size()));
    }
    std::cout << "built " << bres.variants.size() << " variant(s) -> " << libPath << "\n\n";

    // ----- runtime: load the library and select a variant by keyword (engine side) -----
    std::ifstream     in(libPath, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    auto libR = v1::read_library(data);
    if (!libR.isOk())
    {
        std::cerr << "load failed: " << libR.error().message << "\n";
        return 2;
    }
    const auto& lib = libR.value();
    std::cout << "loaded library: " << lib.entries.size() << " entries\n";

    // Compute the variantHash the engine wants: compute stage, USE_SHADOW = 1.
    VariantKey key;
    key.setShaderId("example/material_compute");
    key.setStage(ShaderStage::eComp);
    key.set("USE_SHADOW", 1);
    const uint64_t wantHash = key.build();

    const auto* blob = v1::find(lib, wantHash, ShaderStage::eComp);
    if (!blob)
    {
        std::cerr << "variant (USE_SHADOW=1) not found\n";
        return 3;
    }
    auto binR = v1::read_binary(*blob);
    if (!binR.isOk())
    {
        std::cerr << "binary parse failed: " << binR.error().message << "\n";
        return 4;
    }
    const auto& bin = binR.value();

    std::cout << "selected variant USE_SHADOW=1 (variantHash=" << bin.variantHash << ")\n";
    std::cout << "  stage:        compute\n";
    std::cout << "  spirv words:  " << bin.spirv.size() << "\n";
    std::cout << "  wgsl bytes:   " << bin.wgsl.size() << "\n";
    std::cout << "  local size:   " << bin.reflection.localSizeX << "x" << bin.reflection.localSizeY << "x"
              << bin.reflection.localSizeZ << "\n";
    std::cout << "  descriptors:  " << bin.reflection.descriptors.size() << "\n";
    for (const auto& d : bin.reflection.descriptors)
        std::cout << "    - " << d.name << " set=" << d.set << " binding=" << d.binding << " ("
                  << kind_name(d.kind) << ")\n";
    std::cout << "  material:     " << bin.materialDesc.materialBlockName << " ("
              << bin.materialDesc.materialParamSize << " bytes, " << bin.materialDesc.params.size()
              << " params)\n";
    for (const auto& p : bin.materialDesc.params)
        std::cout << "    - " << p.name << " offset=" << p.offset << " size=" << p.size << "\n";

    return 0;
}
