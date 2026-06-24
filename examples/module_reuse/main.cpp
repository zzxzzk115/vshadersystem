// module_reuse: package shared .slang modules into a .vshslang source pack, then compile
// a shader that `import`s them -- demonstrating library reuse and dependency tracking.

#include <vshaderc/slang_build.hpp>
#include <vshaderc/slang_compiler.hpp>

#include <vshadersystem/vsh_format.hpp>

#include <iostream>

using namespace vshadersystem;

// Shared engine shader library modules (would live in their own .slang files).
static const char* kBrdf = R"SLANG(
module brdf;
public float3 lambert(float3 albedo, float3 n, float3 l)
{
    return albedo * max(dot(n, l), 0.0);
}
)SLANG";

static const char* kColor = R"SLANG(
module color;
import brdf;          // a library module importing another library module
public float3 to_srgb(float3 c) { return pow(max(c, 0.0), (float3)(1.0 / 2.2)); }
)SLANG";

// The project shader that consumes the library.
static const char* kShader = R"SLANG(
import brdf;
import color;

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    float3 lit = lambert((float3)0.8, float3(0, 0, 1), float3(0, 0, 1));
    return float4(to_srgb(lit), 1);
}
)SLANG";

int main()
{
    namespace v1 = vshadersystem::v1;

    // --- pack the shared modules into a .vshslang source library ---
    std::vector<v1::SourceFile> modules = {{"brdf.slang", kBrdf}, {"color.slang", kColor}};
    auto                        pack = v1::write_source_pack(modules);
    std::cout << "Packed " << modules.size() << " modules into a .vshslang library ("
              << pack.value().size() << " bytes)\n";

    // --- mount the pack into the compiler VFS and build the consumer shader ---
    auto unpacked = v1::read_source_pack(pack.value()).value();
    std::vector<vshaderc::SlangSourceFile> vfs;
    for (const auto& f : unpacked)
        vfs.push_back({f.path, f.text});

    vshaderc::SlangCompiler      compiler;
    vshaderc::ShaderBuildOptions bo;
    bo.shaderId          = "examples/surface";
    bo.compile.emitWgsl  = true;
    bo.compile.vfsFiles  = vfs;

    auto br = vshaderc::build_shader(compiler, "surface", "surface.slang", kShader, bo);
    if (!br.isOk())
    {
        std::cerr << "build failed: " << br.error().message << "\n";
        return 1;
    }

    std::cout << "Compiled consumer shader using imported modules: "
              << br.value().variants.size() << " variant(s)\n";

    // build_shader does not surface dependencies; show them via a direct compile.
    auto cr = compiler.compileModule("surface", "surface.slang", kShader, bo.compile);
    if (cr.isOk())
    {
        std::cout << "Resolved dependencies:\n";
        for (const auto& d : cr.value().dependencies)
            std::cout << "  " << d << "\n";
    }

    return 0;
}
