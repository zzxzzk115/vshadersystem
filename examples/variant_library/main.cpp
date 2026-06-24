// variant_library: expand a shader's keyword permutations into a .vshlib, then load it
// the way an engine would and select a specific variant by its keyword set.

#include <vshaderc/slang_build.hpp>
#include <vshaderc/slang_compiler.hpp>

#include <vshadersystem/variant_key.hpp>
#include <vshadersystem/vsh_format.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace vshadersystem;

static const char* kShader = R"SLANG(
import vsh;

[VshKeyword("USE_SHADOW", "bool", "permute", "global")]
[VshKeyword("QUALITY", "enum:low,medium,high", "permute", "local")]
void __vsh_meta() {}

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    float3 c = (float3)1;
#if USE_SHADOW
    c *= 0.5;
#endif
#if QUALITY == 2
    c = pow(c, (float3)2.2);
#endif
    return float4(c, 1);
}
)SLANG";

int main()
{
    namespace v1 = vshadersystem::v1;

    // --- offline: build all variants and write a library ---
    vshaderc::SlangCompiler      compiler;
    vshaderc::ShaderBuildOptions bo;
    bo.shaderId         = "examples/lit";
    bo.compile.emitWgsl = true;

    auto br = vshaderc::build_shader(compiler, "lit", "lit.slang", kShader, bo);
    if (!br.isOk())
    {
        std::cerr << "build failed: " << br.error().message << "\n";
        return 1;
    }
    const auto& res = br.value();

    std::vector<v1::LibraryEntry> entries;
    for (const auto& v : res.variants)
        entries.push_back(
            {v.variantHash, v.stage, v1::write_binary(vshaderc::to_shader_binary(v, res.shaderIdHash, res.keywords)).value()});

    const std::string libPath = "lit.vshlib";
    {
        auto          bytes = v1::write_library(entries, nullptr);
        std::ofstream f(libPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.value().data()),
                static_cast<std::streamsize>(bytes.value().size()));
    }
    std::cout << "Built " << res.combinations << " combinations -> " << res.variants.size()
              << " variants -> " << libPath << "\n\n";

    // --- runtime: load + enumerate keywords + select variants ---
    std::ifstream        in(libPath, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto                 lib = v1::read_library(data).value();
    std::cout << "Loaded library: " << lib.entries.size() << " entries\n";

    // The editor reads a binary to learn which features this shader exposes.
    auto first = v1::read_binary(lib.entries.front().blob).value();
    std::cout << "Shader keywords:\n";
    for (const auto& k : first.keywords)
    {
        std::cout << "  " << k.name;
        if (k.kind == KeywordValueKind::eEnum)
        {
            std::cout << " {";
            for (size_t i = 0; i < k.enumValues.size(); ++i)
                std::cout << (i ? "," : "") << k.enumValues[i];
            std::cout << "}";
        }
        std::cout << "\n";
    }

    // Select a few specific variants by keyword set, as a renderer would.
    std::cout << "\nVariant lookup:\n";
    struct Combo { int shadow; int quality; };
    for (Combo c : {Combo{0, 0}, Combo{1, 0}, Combo{1, 2}})
    {
        VariantKey key;
        key.setShaderId("examples/lit");
        key.setStage(ShaderStage::eFrag);
        key.set("USE_SHADOW", c.shadow);
        key.set("QUALITY", c.quality);
        const uint64_t h = key.build();
        const auto*    blob = v1::find(lib, h, ShaderStage::eFrag);
        if (!blob)
        {
            std::cout << "  USE_SHADOW=" << c.shadow << " QUALITY=" << c.quality << "  -> MISSING\n";
            continue;
        }
        auto bin = v1::read_binary(*blob).value();
        std::cout << "  USE_SHADOW=" << c.shadow << " QUALITY=" << c.quality << "  -> variantHash=" << h
                  << "  spirv=" << bin.spirv.size() << " words\n";
    }

    return 0;
}
