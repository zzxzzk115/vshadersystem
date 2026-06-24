// vshaderc_smoke: M1 smoke test for the Slang compile core (vshaderc-lib).
//
// Exercises: in-memory VFS `import` of a shared module, multi-entry-point compilation,
// and per-entry-point SPIR-V + WGSL emission (WGSL straight from Slang).
//
// Build: xmake f --vshadersystem_build_compiler=y && xmake build vshaderc_smoke
// Run:   xmake run vshaderc_smoke

#include "vshaderc/slang_compiler.hpp"

#include <cstdio>

using namespace vshaderc;

static const char* kCommon = R"SLANG(
// shared library module, resolved via `import common`
public float3 tonemap(float3 c) { return c / (c + 1.0); }
)SLANG";

static const char* kShader = R"SLANG(
import common;

struct VSOut { float4 pos : SV_Position; float3 col : COLOR0; };

[shader("vertex")]
VSOut vertexMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(p * 2.0 - 1.0, 0, 1);
    o.col = float3(p, 0.5);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOut i) : SV_Target
{
    return float4(tonemap(i.col), 1.0);
}
)SLANG";

static const char* stage_name(ShaderStage s)
{
    switch (s)
    {
        case ShaderStage::eVert: return "vert";
        case ShaderStage::eFrag: return "frag";
        case ShaderStage::eComp: return "comp";
        default: return "other";
    }
}

int main()
{
    SlangCompiler compiler;
    if (!compiler.isValid())
    {
        std::printf("FAILED: compiler invalid\n");
        return 1;
    }

    SlangCompileOptions opt;
    opt.emitSpirv = true;
    opt.emitWgsl  = true;
    opt.vfsFiles.push_back({"common.slang", kCommon});

    auto r = compiler.compileModule("triangle", "triangle.slang", kShader, opt);
    if (!r.isOk())
    {
        std::printf("FAILED: %s\n", r.error().message.c_str());
        return 1;
    }

    const auto& res = r.value();
    if (!res.log.empty())
        std::printf("[log]\n%s\n", res.log.c_str());

    std::printf("=== entry points (%zu) ===\n", res.entryPoints.size());
    bool ok = res.entryPoints.size() == 2;
    for (const auto& ep : res.entryPoints)
    {
        std::printf("  %-12s stage=%-5s spirvWords=%zu wgslBytes=%zu\n", ep.name.c_str(), stage_name(ep.stage),
                    ep.spirv.size(), ep.wgsl.size());
        if (ep.spirv.empty() || ep.wgsl.empty())
            ok = false;
    }

    std::printf("=== dependencies (%zu) ===\n", res.dependencies.size());
    bool sawCommon = false;
    for (const auto& d : res.dependencies)
    {
        std::printf("  %s\n", d.c_str());
        if (d.find("common") != std::string::npos)
            sawCommon = true;
    }

    std::printf("\nRESULT: %s\n", (ok && sawCommon) ? "PASS" : "FAIL");
    return (ok && sawCommon) ? 0 : 2;
}
