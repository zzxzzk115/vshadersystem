// vshaderc_smoke: M1 smoke test for the Slang compile core (vshaderc-lib).
//
// Exercises: in-memory VFS `import` of a shared module, multi-entry-point compilation,
// and per-entry-point SPIR-V + WGSL emission (WGSL straight from Slang).
//
// Build: xmake f --vshadersystem_build_compiler=y && xmake build vshaderc_smoke
// Run:   xmake run vshaderc_smoke

#include "vshaderc/slang_compiler.hpp"
#include "vshaderc/slang_metadata.hpp"

#include <cstdio>

using namespace vshaderc;

static const char* kCommon = R"SLANG(
// shared library module, resolved via `import common`
public float3 tonemap(float3 c) { return c / (c + 1.0); }
)SLANG";

static const char* kShader = R"SLANG(
import common;
import vsh;

[VshMaterial]
struct Material
{
    [VshSemantic("baseColor")]                 float4 baseColorFactor;
    [VshSemantic("metallic")] [VshRange(0, 1)] float  metallicFactor;
    [VshTexture("Texture2D")]                  int    baseColorTex_index;
};

[VshKeyword("USE_SHADOW", "bool", "permute", "global")]
[VshKeyword("QUALITY", "enum:low,medium,high", "permute", "local")]
[VshRenderState("cull", "back")]
[VshRenderState("depth_test", "on")]
void __vsh_meta() {}

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

    // --- metadata extraction (vsh attributes via reflection) ---
    auto mr = extract_shader_metadata(compiler, "triangle", "triangle.slang", kShader, opt);
    if (!mr.isOk())
    {
        std::printf("FAILED (metadata): %s\n", mr.error().message.c_str());
        return 1;
    }
    const auto& meta = mr.value();
    std::printf("\n=== metadata ===\n");
    std::printf("material struct: %s (%zu fields)\n", meta.materialStructName.c_str(), meta.materialFields.size());
    for (const auto& f : meta.materialFields)
        std::printf("  field %-18s semantic=%-10s range=%s texture=%s\n", f.name.c_str(),
                    f.semantic.empty() ? "-" : f.semantic.c_str(),
                    f.hasRange ? "yes" : "no", f.textureKind.empty() ? "-" : f.textureKind.c_str());
    std::printf("keywords: %zu\n", meta.keywords.size());
    for (const auto& k : meta.keywords)
        std::printf("  %-12s kind=%d dispatch=%d scope=%d enums=%zu\n", k.name.c_str(),
                    static_cast<int>(k.kind), static_cast<int>(k.dispatch), static_cast<int>(k.scope),
                    k.enumValues.size());
    std::printf("renderState present=%d cull=%d depthTest=%d (raw %zu)\n",
                static_cast<int>(meta.hasRenderState), static_cast<int>(meta.renderState.cull),
                static_cast<int>(meta.renderState.depthTest), meta.renderStateRaw.size());

    bool metaOk = meta.materialStructName == "Material" && meta.materialFields.size() == 3 &&
                  meta.keywords.size() == 2 && meta.keywords[1].enumValues.size() == 3 &&
                  meta.hasRenderState && meta.renderStateRaw.size() == 2;

    std::printf("\nRESULT: %s\n", (ok && sawCommon && metaOk) ? "PASS" : "FAIL");
    return (ok && sawCommon && metaOk) ? 0 : 2;
}
