// vshaderc_smoke: M1 smoke test for the Slang compile core (vshaderc-lib).
//
// Exercises: in-memory VFS `import` of a shared module, multi-entry-point compilation,
// and per-entry-point SPIR-V + WGSL emission (WGSL straight from Slang).
//
// Build: xmake f --vshadersystem_build_compiler=y && xmake build vshaderc_smoke
// Run:   xmake run vshaderc_smoke

#include "vshaderc/slang_build.hpp"
#include "vshaderc/slang_compiler.hpp"
#include "vshaderc/slang_metadata.hpp"
#include "vshaderc/slang_reflect.hpp"

#include "vshadersystem/vsh_format.hpp"

#include <cstdio>
#include <set>

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

[[vk::binding(0, 0)]] ConstantBuffer<Material> uMat;
[[vk::binding(1, 0)]] Texture2D<float4>        baseColorTex;
[[vk::binding(2, 0)]] SamplerState             samp;

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
    float4 tex = baseColorTex.Sample(samp, i.col.xy);
    return float4(tonemap(i.col), 1.0) * uMat.baseColorFactor * tex;
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

    // --- program reflection (ProgramLayout -> ShaderReflection + MaterialDescription) ---
    auto rr = reflect_shader(compiler, "triangle", "triangle.slang", kShader, opt, meta);
    if (!rr.isOk())
    {
        std::printf("FAILED (reflect): %s\n", rr.error().message.c_str());
        return 1;
    }
    const auto& pr = rr.value();
    std::printf("\n=== reflection ===\n");
    std::printf("descriptors: %zu\n", pr.reflection.descriptors.size());
    for (const auto& d : pr.reflection.descriptors)
        std::printf("  %-14s set=%u binding=%u kind=%d count=%u tex=%d\n", d.name.c_str(), d.set, d.binding,
                    static_cast<int>(d.kind), d.count, static_cast<int>(d.textureType));
    std::printf("blocks: %zu\n", pr.reflection.blocks.size());
    for (const auto& b : pr.reflection.blocks)
    {
        std::printf("  block %-10s set=%u binding=%u size=%u members=%zu\n", b.name.c_str(), b.set, b.binding,
                    b.size, b.members.size());
        for (const auto& m : b.members)
            std::printf("      %-18s offset=%u size=%u type=%d\n", m.name.c_str(), m.offset, m.size,
                        static_cast<int>(m.type));
    }
    std::printf("material: block=%s size=%u params=%zu textures=%zu\n", pr.material.materialBlockName.c_str(),
                pr.material.materialParamSize, pr.material.params.size(), pr.material.textures.size());
    for (const auto& p : pr.material.params)
        std::printf("  param %-18s offset=%u size=%u type=%d semantic=%d range=%d\n", p.name.c_str(), p.offset,
                    p.size, static_cast<int>(p.type), static_cast<int>(p.semantic), static_cast<int>(p.hasRange));
    for (const auto& t : pr.material.textures)
        std::printf("  texture %-16s set=%u binding=%u type=%d semantic=%d\n", t.name.c_str(), t.set, t.binding,
                    static_cast<int>(t.type), static_cast<int>(t.semantic));

    const auto& R = pr.reflection;
    bool        hasUbo = false, hasTex = false, hasSampler = false;
    for (const auto& d : R.descriptors)
    {
        if (d.kind == vshadersystem::DescriptorKind::eUniformBuffer) hasUbo = true;
        if (d.kind == vshadersystem::DescriptorKind::eSampledImage) hasTex = true;
        if (d.kind == vshadersystem::DescriptorKind::eSampler) hasSampler = true;
    }
    bool reflOk = hasUbo && hasTex && hasSampler && R.blocks.size() == 1 &&
                  R.blocks[0].members.size() == 3 && pr.material.params.size() == 2 &&
                  pr.material.textures.size() == 1 && pr.material.materialParamSize > 0 &&
                  pr.material.renderState.cull == vshadersystem::CullMode::eBack;

    // --- variant expansion (USE_SHADOW{2} x QUALITY{3} = 6 combos x 2 stages = 12) ---
    ShaderBuildOptions bopt;
    bopt.compile  = opt;
    bopt.shaderId = "example/triangle";
    auto br       = build_shader(compiler, "triangle", "triangle.slang", kShader, bopt);
    if (!br.isOk())
    {
        std::printf("FAILED (build): %s\n", br.error().message.c_str());
        return 1;
    }
    const auto& bres = br.value();
    std::printf("\n=== variants ===\n");
    std::printf("combinations=%u skipped=%u variants=%zu\n", bres.combinations, bres.skipped,
                bres.variants.size());
    std::set<uint64_t> vertHashes, fragHashes;
    for (const auto& v : bres.variants)
    {
        if (v.stage == vshadersystem::ShaderStage::eVert) vertHashes.insert(v.variantHash);
        if (v.stage == vshadersystem::ShaderStage::eFrag) fragHashes.insert(v.variantHash);
    }
    std::printf("distinct vert hashes=%zu frag hashes=%zu\n", vertHashes.size(), fragHashes.size());

    bool buildOk = bres.combinations == 6 && bres.variants.size() == 12 && vertHashes.size() == 6 &&
                   fragHashes.size() == 6;

    // --- new format round-trip: variants -> .vshlib bytes -> read -> lookup -> .vshbin ---
    namespace v1 = vshadersystem::v1;
    std::vector<v1::LibraryEntry> entries;
    for (const auto& v : bres.variants)
    {
        auto binBytes = v1::write_binary(vshaderc::to_shader_binary(v, bres.shaderIdHash));
        if (!binBytes.isOk())
        {
            std::printf("FAILED (write_binary): %s\n", binBytes.error().message.c_str());
            return 1;
        }
        entries.push_back({v.variantHash, v.stage, binBytes.value()});
    }
    auto libBytes = v1::write_library(entries, nullptr);
    auto libR     = libBytes.isOk() ? v1::read_library(libBytes.value())
                                    : vshadersystem::Result<v1::Library>::err(libBytes.error());
    bool fmtOk = false;
    if (libR.isOk())
    {
        const auto& lib = libR.value();
        // pick the first frag variant and round-trip it
        const vshaderc::VariantBinary* pick = nullptr;
        for (const auto& v : bres.variants)
            if (v.stage == vshadersystem::ShaderStage::eFrag)
            {
                pick = &v;
                break;
            }
        const auto* blob = pick ? v1::find(lib, pick->variantHash, pick->stage) : nullptr;
        if (blob)
        {
            auto rb = v1::read_binary(*blob);
            if (rb.isOk())
            {
                const auto& sb = rb.value();
                fmtOk = lib.entries.size() == 12 && sb.variantHash == pick->variantHash &&
                        sb.stage == vshadersystem::ShaderStage::eFrag && !sb.spirv.empty() &&
                        !sb.wgsl.empty() && sb.materialDesc.params.size() == 2 &&
                        sb.reflection.descriptors.size() == 3;
                std::printf("\n=== format round-trip ===\n");
                std::printf("lib bytes=%zu entries=%zu | frag binary: spirv=%zu wgsl=%zu params=%zu desc=%zu\n",
                            libBytes.value().size(), lib.entries.size(), sb.spirv.size(), sb.wgsl.size(),
                            sb.materialDesc.params.size(), sb.reflection.descriptors.size());
            }
        }
    }

    bool pass = ok && sawCommon && metaOk && reflOk && buildOk && fmtOk;
    std::printf("\nRESULT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 2;
}
