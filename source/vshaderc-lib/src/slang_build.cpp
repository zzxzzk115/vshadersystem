#include "vshaderc/slang_build.hpp"

#include "vshaderc/slang_metadata.hpp"
#include "vshaderc/slang_reflect.hpp"

#include "vshadersystem/hash.hpp"
#include "vshadersystem/keyword_expr.hpp"
#include "vshadersystem/shader_id.hpp"
#include "vshadersystem/variant_key.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

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
        std::vector<KeywordDecl> collect_permute_keywords(const ShaderMetadata& meta, const EngineKeywordsFile* engine)
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

        uint64_t total = 1;
        for (const auto& k : perm)
        {
            const auto domain = keyword_domain(k);
            if (total > opt.maxVariants / domain)
                return R::err(
                    {ErrorCode::eCompileError, "permutation explosion exceeds cap " + std::to_string(opt.maxVariants)});
            total *= domain;
        }
        if (total > opt.maxVariants)
            return R::err(
                {ErrorCode::eCompileError, "permutation explosion exceeds cap " + std::to_string(opt.maxVariants)});
        result.combinations     = static_cast<uint32_t>(total);
        result.fileDependencies = meta.fileDependencies;

        struct Job
        {
            std::vector<std::pair<std::string, uint32_t>> values;
            bool                                          valid = true;
        };
        struct Output
        {
            std::vector<VariantBinary>  variants;
            std::vector<FileDependency> dependencies;
            Error                       error;
        };
        std::unordered_map<std::string, const KeywordDecl*> declMap;
        for (const auto& k : perm)
            declMap[k.name] = &k;
        std::vector<Job>      jobs(static_cast<size_t>(total));
        std::vector<Output>   outputs(jobs.size());
        std::vector<uint32_t> idx(perm.size(), 0);
        for (auto& job : jobs)
        {
            KeywordValueContext ctx;
            ctx.decls = declMap;
            for (size_t i = 0; i < perm.size(); ++i)
            {
                job.values.emplace_back(perm[i].name, idx[i]);
                ctx.values[perm[i].name] = idx[i];
            }
            for (const auto& k : perm)
            {
                if (k.constraint.empty())
                    continue;
                auto ev = eval_only_if(k.constraint, ctx);
                if (ev.isOk() && !ev.value())
                {
                    job.valid = false;
                    ++result.skipped;
                    break;
                }
            }
            for (size_t i = 0; i < perm.size(); ++i)
            {
                if (++idx[i] < keyword_domain(perm[i]))
                    break;
                idx[i] = 0;
            }
        }

        std::mutex        callbackMutex;
        std::atomic<bool> failed {false};
        auto              execute = [&](SlangCompiler& worker, size_t i) {
            const auto& job    = jobs[i];
            auto&       output = outputs[i];
            try
            {
                if (opt.onVariant)
                {
                    std::lock_guard lock(callbackMutex);
                    opt.onVariant(static_cast<uint32_t>(i + 1), result.combinations, job.values, !job.valid);
                }
                if (!job.valid)
                    return;
                SlangCompileOptions co = opt.compile;
                for (const auto& kv : job.values)
                    co.defines.push_back({kv.first, std::to_string(kv.second)});
                auto cr = worker.compileModule(moduleName, modulePath, moduleSource, co, &meta);
                if (!cr.isOk())
                {
                    output.error = cr.error();
                    failed       = true;
                    return;
                }
                output.dependencies = std::move(cr.value().fileDependencies);
                for (auto& ep : cr.value().entryPoints)
                {
                    if (ep.stage == ShaderStage::eUnknown)
                        continue;
                    VariantKey key;
                    key.setShaderIdHash(result.shaderIdHash);
                    key.setStage(ep.stage);
                    for (const auto& kv : job.values)
                        key.set(kv.first, kv.second);
                    VariantBinary vb;
                    vb.stage          = ep.stage;
                    vb.entryPointName = ep.name;
                    vb.keywordValues  = job.values;
                    vb.variantHash    = key.build();
                    vb.spirv          = std::move(ep.spirv);
                    vb.wgsl           = std::move(ep.wgsl);
                    vb.dxbc           = std::move(ep.dxbc);
                    vb.dxil           = std::move(ep.dxil);
                    vb.reflection     = cr.value().reflection;
                    vb.material       = cr.value().material;
                    output.variants.push_back(std::move(vb));
                }
            }
            catch (const std::exception& ex)
            {
                output.error = {ErrorCode::eCompileError, ex.what()};
                failed       = true;
            }
        };
        Error        workerError;
        const size_t count = std::min<size_t>(std::clamp(opt.jobs, 1u, 32u), jobs.size());
        if (count <= 1)
        {
            for (size_t i = 0; i < jobs.size() && !failed; ++i)
                execute(compiler, i);
        }
        else
        {
            std::atomic<size_t> next {0};
            auto                reportWorkerError = [&](const std::exception& ex) {
                std::lock_guard lock(callbackMutex);
                if (workerError.code == ErrorCode::eOk)
                    workerError = {ErrorCode::eCompileError, ex.what()};
                failed = true;
            };
            // Destroy/join threads before their error-reporting closure.
            std::vector<std::jthread> workers;
            try
            {
                for (size_t i = 0; i < count; ++i)
                    workers.emplace_back([&] {
                        try
                        {
                            // Slang's global session is not thread-safe. Never share the caller's session.
                            SlangCompiler worker;
                            while (!failed)
                            {
                                const auto j = next.fetch_add(1);
                                if (j >= jobs.size())
                                    break;
                                execute(worker, j);
                            }
                        }
                        catch (const std::exception& ex)
                        {
                            reportWorkerError(ex);
                        }
                    });
            }
            catch (const std::exception& ex)
            {
                reportWorkerError(ex);
            }
        } // jthread joins before outputs or metadata are read/destroyed
        if (workerError.code != ErrorCode::eOk)
            return R::err(workerError);
        for (auto& output : outputs)
        {
            if (output.error.code != ErrorCode::eOk)
                return R::err(output.error);
            for (auto& variant : output.variants)
                result.variants.push_back(std::move(variant));
            result.fileDependencies.insert(
                result.fileDependencies.end(), output.dependencies.begin(), output.dependencies.end());
        }
        std::sort(result.fileDependencies.begin(), result.fileDependencies.end());
        result.fileDependencies.erase(std::unique(result.fileDependencies.begin(), result.fileDependencies.end()),
                                      result.fileDependencies.end());

        return R::ok(std::move(result));
    }

    ShaderBinary
    to_shader_binary(const VariantBinary& v, uint64_t shaderIdHash, const std::vector<KeywordDecl>& keywords)
    {
        ShaderBinary b;
        b.shaderIdHash   = shaderIdHash;
        b.variantHash    = v.variantHash;
        b.stage          = v.stage;
        b.entryPointName = v.entryPointName;
        b.spirv          = v.spirv;
        b.wgsl           = v.wgsl;
        b.dxbc           = v.dxbc;
        b.dxil           = v.dxil;
        b.spirvHash      = v.spirv.empty() ? 0 : xxhash64_words(v.spirv);
        b.reflection     = v.reflection;
        b.materialDesc   = v.material;
        b.keywords       = keywords;
        return b;
    }
} // namespace vshaderc
