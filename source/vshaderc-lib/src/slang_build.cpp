#include "vshaderc/slang_build.hpp"

#include "vshaderc/slang_metadata.hpp"
#include "vshaderc/slang_reflect.hpp"

#include "vshadersystem/hash.hpp"
#include "vshadersystem/keyword_expr.hpp"
#include "vshadersystem/shader_id.hpp"
#include "vshadersystem/variant_key.hpp"

#include <string>

namespace vshaderc
{
    using namespace vshadersystem;

    namespace
    {
        uint32_t keyword_domain(const KeywordDecl& k)
        {
            if (k.kind == KeywordValueKind::eEnum)
                return k.enumValues.empty() ? 1u : static_cast<uint32_t>(k.enumValues.size());
            return 2u; // bool: {0,1}
        }

        // Collect permute keywords: engine decls first, then shader decls (shader wins on
        // name collision so a shader can refine an engine keyword).
        std::vector<KeywordDecl> collect_permute_keywords(const ShaderMetadata&     meta,
                                                          const EngineKeywordsFile* engine)
        {
            std::vector<KeywordDecl> out;
            auto                     add = [&](const KeywordDecl& k) {
                if (k.dispatch != KeywordDispatch::ePermutation)
                    return;
                for (auto& e : out)
                    if (e.name == k.name)
                    {
                        e = k;
                        return;
                    }
                out.push_back(k);
            };
            if (engine)
                for (const auto& k : engine->decls)
                    add(k);
            for (const auto& k : meta.keywords)
                add(k);
            return out;
        }
    } // namespace

    Result<ShaderBuildResult> build_shader(SlangCompiler&            compiler,
                                           const std::string&        moduleName,
                                           const std::string&        modulePath,
                                           const std::string&        moduleSource,
                                           const ShaderBuildOptions& opt)
    {
        using R = Result<ShaderBuildResult>;

        auto metaR = extract_shader_metadata(compiler, moduleName, modulePath, moduleSource, opt.compile);
        if (!metaR.isOk())
            return R::err(metaR.error());
        const ShaderMetadata& meta = metaR.value();

        ShaderBuildResult result;
        result.shaderId     = opt.shaderId;
        result.shaderIdHash = shader_id_hash(opt.shaderId);
        result.keywords     = meta.keywords;

        const std::vector<KeywordDecl> perm = collect_permute_keywords(meta, opt.engineKeywords);

        // Total combination count (product of domains), guarded by maxVariants.
        uint64_t total = 1;
        for (const auto& k : perm)
            total *= keyword_domain(k);
        if (total > opt.maxVariants)
            return R::err({ErrorCode::eCompileError, "permutation explosion: " + std::to_string(total) +
                                                         " combinations exceeds cap " +
                                                         std::to_string(opt.maxVariants)});
        result.combinations = static_cast<uint32_t>(total);

        // Constraint context: decls map stays constant; values updated per combo.
        std::unordered_map<std::string, const KeywordDecl*> declMap;
        for (const auto& k : perm)
            declMap[k.name] = &k;

        // Iterate the cartesian product via a mixed-radix counter.
        std::vector<uint32_t> idx(perm.size(), 0);
        for (uint64_t combo = 0; combo < total; ++combo)
        {
            // Resolve this combination's keyword values.
            std::vector<std::pair<std::string, uint32_t>> kvs;
            KeywordValueContext                           ctx;
            ctx.decls = declMap;
            kvs.reserve(perm.size());
            for (size_t i = 0; i < perm.size(); ++i)
            {
                kvs.emplace_back(perm[i].name, idx[i]);
                ctx.values[perm[i].name] = idx[i];
            }

            // Constraint pruning (only_if on engine keywords).
            bool valid = true;
            for (const auto& k : perm)
            {
                if (k.constraint.empty())
                    continue;
                auto ev = eval_only_if(k.constraint, ctx);
                if (ev.isOk() && !ev.value())
                {
                    valid = false;
                    break;
                }
            }

            if (valid)
            {
                // Per-variant compile options: base + keyword macros (value-based; shaders
                // use `#if KEYWORD` / `#if KEYWORD == N`).
                SlangCompileOptions co = opt.compile;
                for (const auto& kv : kvs)
                    co.defines.push_back({kv.first, std::to_string(kv.second)});

                auto cr = compiler.compileModule(moduleName, modulePath, moduleSource, co);
                if (!cr.isOk())
                    return R::err(cr.error());

                auto rr = reflect_shader(compiler, moduleName, modulePath, moduleSource, co, meta);
                if (!rr.isOk())
                    return R::err(rr.error());

                for (const auto& ep : cr.value().entryPoints)
                {
                    if (ep.stage == ShaderStage::eUnknown)
                        continue;
                    VariantKey key;
                    key.setShaderIdHash(result.shaderIdHash);
                    key.setStage(ep.stage);
                    for (const auto& kv : kvs)
                        key.set(kv.first, kv.second);

                    VariantBinary vb;
                    vb.stage          = ep.stage;
                    vb.entryPointName = ep.name;
                    vb.keywordValues  = kvs;
                    vb.variantHash    = key.build();
                    vb.spirv          = ep.spirv;
                    vb.wgsl           = ep.wgsl;
                    vb.reflection     = rr.value().reflection;
                    vb.material       = rr.value().material;
                    result.variants.push_back(std::move(vb));
                }
            }
            else
            {
                ++result.skipped;
            }

            // Advance mixed-radix counter.
            for (size_t i = 0; i < perm.size(); ++i)
            {
                if (++idx[i] < keyword_domain(perm[i]))
                    break;
                idx[i] = 0;
            }
        }

        return R::ok(std::move(result));
    }

    ShaderBinary to_shader_binary(const VariantBinary& v, uint64_t shaderIdHash,
                                  const std::vector<KeywordDecl>& keywords)
    {
        ShaderBinary b;
        b.shaderIdHash   = shaderIdHash;
        b.variantHash    = v.variantHash;
        b.stage          = v.stage;
        b.entryPointName = v.entryPointName;
        b.spirv          = v.spirv;
        b.wgsl           = v.wgsl;
        b.spirvHash      = v.spirv.empty() ? 0 : xxhash64_words(v.spirv);
        b.reflection     = v.reflection;
        b.materialDesc   = v.material;
        b.keywords       = keywords;
        return b;
    }
} // namespace vshaderc
