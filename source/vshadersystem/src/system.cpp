#include "vshadersystem/system.hpp"
#include "vshadersystem/binary.hpp"
#include "vshadersystem/compiler.hpp"
#include "vshadersystem/hash.hpp"
#include "vshadersystem/metadata.hpp"
#include "vshadersystem/reflect.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace vshadersystem
{
    static inline bool parse_bool_value(std::string_view s, uint32_t& out)
    {
        if (s.empty())
        {
            out = 1;
            return true;
        }
        if (s == "1" || s == "true" || s == "TRUE" || s == "True")
        {
            out = 1;
            return true;
        }
        if (s == "0" || s == "false" || s == "FALSE" || s == "False")
        {
            out = 0;
            return true;
        }
        return false;
    }

    static inline Result<uint32_t> parse_keyword_value(const KeywordDecl& d, std::string_view raw)
    {
        if (d.kind == KeywordValueKind::eBool)
        {
            uint32_t v = 0;
            if (!parse_bool_value(raw, v))
                return Result<uint32_t>::err(
                    {ErrorCode::eParseError, "Invalid bool value for keyword '" + d.name + "'"});
            return Result<uint32_t>::ok(v);
        }

        // Enum
        if (raw.empty())
            return Result<uint32_t>::ok(d.defaultValue);

        // Accept numeric index
        {
            bool     ok  = true;
            uint32_t idx = 0;
            for (char c : raw)
            {
                if (c < '0' || c > '9')
                {
                    ok = false;
                    break;
                }
                idx = idx * 10u + static_cast<uint32_t>(c - '0');
            }
            if (ok)
            {
                if (idx >= d.enumValues.size())
                    return Result<uint32_t>::err(
                        {ErrorCode::eParseError, "Enum index out of range for keyword '" + d.name + "'"});
                return Result<uint32_t>::ok(idx);
            }
        }

        // Accept enumerant name
        for (uint32_t i = 0; i < static_cast<uint32_t>(d.enumValues.size()); ++i)
        {
            if (d.enumValues[i] == raw)
                return Result<uint32_t>::ok(i);
        }

        return Result<uint32_t>::err(
            {ErrorCode::eParseError, "Unknown enum value '" + std::string(raw) + "' for keyword '" + d.name + "'"});
    }

    static inline Result<uint64_t> compute_variant_hash(const ParsedMetadata&     meta,
                                                        const CompileOptions&     opt,
                                                        const EngineKeywordsFile* engineKw,
                                                        uint64_t                  sourceHash)
    {
        // Only permutation keywords contribute to the compiled variant hash.
        // Runtime keywords are represented by runtime parameters.
        // Special keywords are intended for specialization constants and do not require separate SPIR-V blobs.

        struct KV
        {
            uint64_t nameHash;
            uint32_t value;
        };

        std::vector<KV> kvs;
        kvs.reserve(meta.keywords.size());

        // Build a define map for fast lookup
        std::unordered_map<std::string, std::string> defMap;
        defMap.reserve(opt.defines.size());
        for (const auto& d : opt.defines)
            defMap[d.name] = d.value;

        for (const auto& k : meta.keywords)
        {
            if (k.dispatch != KeywordDispatch::ePermutation)
                continue;

            // Resolution order:
            //   - Compile defines (-D)
            //   - engine_keywords.vkw set (global scope only)
            //   - shader default
            uint32_t value = k.defaultValue;

            auto it = defMap.find(k.name);
            if (it != defMap.end())
            {
                auto pv = parse_keyword_value(k, it->second);
                if (!pv.isOk())
                    return Result<uint64_t>::err(pv.error());
                value = pv.value();
            }
            else if (engineKw && k.scope == KeywordScope::eGlobal)
            {
                auto iv = engineKw->values.find(k.name);
                if (iv != engineKw->values.end())
                {
                    auto pv = parse_keyword_value(k, iv->second);
                    if (!pv.isOk())
                        return Result<uint64_t>::err(pv.error());
                    value = pv.value();
                }
            }

            KV kv;
            kv.nameHash = xxhash64(k.name);
            kv.value    = value;
            kvs.push_back(kv);
        }

        std::sort(kvs.begin(), kvs.end(), [](const KV& a, const KV& b) {
            if (a.nameHash != b.nameHash)
                return a.nameHash < b.nameHash;
            return a.value < b.value;
        });

        // Serialize into a small stable buffer for hashing
        std::vector<uint8_t> buf;
        buf.reserve(32 + kvs.size() * 16);

        auto append_u64 = [&](uint64_t v) {
            uint8_t b[8];
            std::memcpy(b, &v, 8);
            buf.insert(buf.end(), b, b + 8);
        };
        auto append_u32 = [&](uint32_t v) {
            uint8_t b[4];
            std::memcpy(b, &v, 4);
            buf.insert(buf.end(), b, b + 4);
        };

        append_u64(sourceHash);
        append_u32(static_cast<uint32_t>(static_cast<uint8_t>(opt.stage)));
        append_u32(static_cast<uint32_t>(kvs.size()));
        for (const auto& kv : kvs)
        {
            append_u64(kv.nameHash);
            append_u32(kv.value);
            append_u32(0); // reserved for future (e.g., scope, width)
        }

        return Result<uint64_t>::ok(xxhash64(buf.data(), buf.size()));
    }

    static inline std::string normalize_define_list(const std::vector<Define>& defs)
    {
        // Deterministic ordering for stable cache keys.
        std::vector<std::string> lines;
        lines.reserve(defs.size());
        for (const auto& d : defs)
        {
            if (d.value.empty())
                lines.push_back(d.name);
            else
                lines.push_back(d.name + "=" + d.value);
        }
        std::sort(lines.begin(), lines.end());
        std::string out;
        for (auto& s : lines)
        {
            out += s;
            out.push_back('\n');
        }
        return out;
    }

    static inline uint64_t
    compute_build_hash(const SourceInput& src, const CompileOptions& opt, const ParsedMetadata& meta)
    {
        // v1: hash (source text + stage + include dirs + defines + normalized metadata tokens).
        // We can extend this with expanded include contents once a custom includer records dependencies.
        uint64_t h = 0;
        h          = xxhash64(src.sourceText, h);
        h          = xxhash64(src.virtualPath, h);

        h = xxhash64(&opt.stage, sizeof(opt.stage), h);

        auto defs = normalize_define_list(opt.defines);
        h         = xxhash64(defs, h);

        for (const auto& dir : opt.includeDirs)
            h = xxhash64(dir, h);

        // Metadata normalization: we only hash what affects the binary artifact.
        // Semantics/default/range/state are embedded in .vshbin, so they must be part of cache key.
        {
            std::string m;
            m.reserve(256);
            m += meta.hasMaterialDecl ? "material=1\n" : "material=0\n";
            m += "depthTest=" + std::to_string(meta.renderState.depthTest) + "\n";
            m += "depthWrite=" + std::to_string(meta.renderState.depthWrite) + "\n";
            m += "depthFunc=" + std::to_string(static_cast<int>(meta.renderState.depthFunc)) + "\n";
            m += "cull=" + std::to_string(static_cast<int>(meta.renderState.cull)) + "\n";
            m += "blendEnable=" + std::to_string(meta.renderState.blendEnable) + "\n";
            m += "srcColor=" + std::to_string(static_cast<int>(meta.renderState.srcColor)) + "\n";
            m += "dstColor=" + std::to_string(static_cast<int>(meta.renderState.dstColor)) + "\n";
            m += "colorOp=" + std::to_string(static_cast<int>(meta.renderState.colorOp)) + "\n";
            m += "srcAlpha=" + std::to_string(static_cast<int>(meta.renderState.srcAlpha)) + "\n";
            m += "dstAlpha=" + std::to_string(static_cast<int>(meta.renderState.dstAlpha)) + "\n";
            m += "alphaOp=" + std::to_string(static_cast<int>(meta.renderState.alphaOp)) + "\n";
            m += "colorMask=" + std::to_string(static_cast<int>(meta.renderState.colorMask)) + "\n";
            m += "alphaToCoverage=" + std::to_string(meta.renderState.alphaToCoverage) + "\n";
            m += "depthBiasFactor=" + std::to_string(meta.renderState.depthBiasFactor) + "\n";
            m += "depthBiasUnits=" + std::to_string(meta.renderState.depthBiasUnits) + "\n";

            // params
            std::vector<std::string> keys;
            keys.reserve(meta.params.size());
            for (const auto& [k, _] : meta.params)
                keys.push_back(k);
            std::sort(keys.begin(), keys.end());
            for (auto& k : keys)
            {
                const auto& pm = meta.params.at(k);
                m += "p:" + k + ":sem=" + std::to_string(static_cast<uint32_t>(pm.semantic)) + "\n";
                if (pm.hasDefault)
                {
                    m += "p:" + k + ":def=";
                    for (uint8_t i : pm.defaultValue.valueBuffer)
                    {
                        m += std::to_string(i);
                        m.push_back(',');
                    }
                    m.push_back('\n');
                }
                if (pm.hasRange)
                {
                    m +=
                        "p:" + k + ":range=" + std::to_string(pm.range.min) + "," + std::to_string(pm.range.max) + "\n";
                }
            }

            // textures
            keys.clear();
            keys.reserve(meta.textures.size());
            for (const auto& [k, _] : meta.textures)
                keys.push_back(k);
            std::sort(keys.begin(), keys.end());
            for (auto& k : keys)
            {
                const auto& tm = meta.textures.at(k);
                m += "t:" + k + ":sem=" + std::to_string(static_cast<uint32_t>(tm.semantic)) + "\n";
            }

            h = xxhash64(m, h);
        }

        return h;
    }

    static inline std::string cache_path(const std::string& cacheDir, uint64_t buildHash)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(buildHash));
        return (std::filesystem::path(cacheDir) / (std::string(buf) + ".vshbin")).string();
    }

    static inline Result<void>
    validate_and_build_mdesc(MaterialDescription& mdesc, const ShaderReflection& refl, const ParsedMetadata& meta)
    {
        // v1 policy:
        // - Material parameters are members of a UBO block named "Material" (or mdesc.materialBlockName).
        // - Textures are sampled image descriptors (combined image sampler or separate image).
        //
        // We can relax this later by allowing multiple blocks or explicit pragma binding hints.

        const std::string blockName = mdesc.materialBlockName;

        const BlockLayout* matBlock = nullptr;
        for (const auto& b : refl.blocks)
        {
            if (!b.isPushConstant && b.name == blockName)
            {
                matBlock = &b;
                break;
            }
        }

        if (!matBlock)
        {
            // No material block: valid shader case (fullscreen, compute, raytracing, etc.)
            mdesc.materialParamSize = 0;
            mdesc.params.clear();
        }
        else
        {
            mdesc.materialParamSize = matBlock->size;

            // Params: from reflected members
            mdesc.params.clear();
            mdesc.params.reserve(matBlock->members.size());

            for (const auto& mem : matBlock->members)
            {
                MaterialParamDesc pd;
                pd.name   = mem.name;
                pd.offset = mem.offset;
                pd.size   = mem.size;
                pd.type   = mem.type;

                auto it = meta.params.find(mem.name);
                if (it != meta.params.end())
                {
                    pd.semantic = it->second.semantic;

                    if (it->second.hasDefault)
                    {
                        pd.hasDefault        = true;
                        pd.defaultValue      = it->second.defaultValue;
                        pd.defaultValue.type = pd.type;
                    }
                    if (it->second.hasRange)
                    {
                        pd.hasRange = true;
                        pd.range    = it->second.range;
                    }
                }

                mdesc.params.push_back(std::move(pd));
            }

            // Textures: from descriptors (sampled images / combined)
            mdesc.textures.clear();
            for (const auto& d : refl.descriptors)
            {
                const bool isTexture =
                    (d.kind == DescriptorKind::eCombinedImageSampler) || (d.kind == DescriptorKind::eSampledImage);

                if (!isTexture)
                    continue;

                MaterialTextureDesc td;
                td.name    = d.name;
                td.set     = d.set;
                td.binding = d.binding;
                td.count   = d.count;
                td.type    = TextureType::eUnknown; // v1: we can refine with spirv-cross type info later

                auto it = meta.textures.find(d.name);
                if (it != meta.textures.end())
                    td.semantic = it->second.semantic;

                mdesc.textures.push_back(std::move(td));
            }

            // Render state
            mdesc.renderState = meta.renderState;

            // Validation: metadata tokens must map to reflected symbols (strict)
            if (matBlock)
            {
                for (const auto& [name, _] : meta.params)
                {
                    bool found = false;
                    for (const auto& mem : matBlock->members)
                    {
                        if (mem.name == name)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        return Result<void>::err(
                            {ErrorCode::eParseError,
                             "Metadata param '" + name + "' not found in Material block members."});
                }
            }
            else
            {
                if (!meta.params.empty())
                {
                    return Result<void>::err(
                        {ErrorCode::eParseError, "Shader declares metadata params but has no Material block."});
                }
            }
        }

        for (const auto& [name, _] : meta.textures)
        {
            bool found = false;
            for (const auto& d : refl.descriptors)
            {
                if (d.name == name)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                return Result<void>::err(
                    {ErrorCode::eParseError, "Metadata texture '" + name + "' not found in reflected descriptors."});
        }

        return Result<void>::ok();
    }

    Result<BuildResult> build_shader(const BuildRequest& req)
    {
        // Parse metadata first, so it can contribute to cache key even if compilation fails later.
        auto metaR = parse_vultra_metadata(req.source.sourceText);
        if (!metaR.isOk())
            return Result<BuildResult>::err(metaR.error());
        const ParsedMetadata meta = std::move(metaR.value());

        const uint64_t buildHash  = compute_build_hash(req.source, req.options, meta);
        const uint64_t sourceHash = xxhash64(req.source.sourceText);

        BuildResult out;
        out.fromCache = false;

        if (req.enableCache)
        {
            const std::string path   = cache_path(req.cacheDir, buildHash);
            auto              cached = read_vshbin_file(path);
            if (cached.isOk())
            {
                out.binary    = std::move(cached.value());
                out.log       = "Cache hit: " + path;
                out.fromCache = true;
                return Result<BuildResult>::ok(std::move(out));
            }
        }

        // Compile
        auto c = compile_glsl_to_spirv(req.source, req.options);
        if (!c.isOk())
            return Result<BuildResult>::err(c.error());

        // Reflect
        auto r = reflect_spirv(c.value().spirv);
        if (!r.isOk())
            return Result<BuildResult>::err(r.error());

        ShaderBinary bin;
        bin.stage       = req.options.stage;
        bin.spirv       = std::move(c.value().spirv);
        bin.spirvHash   = xxhash64_words(bin.spirv);
        bin.contentHash = sourceHash;
        bin.reflection  = std::move(r.value());

        // Compute variant hash (permutation keywords only)
        {
            const EngineKeywordsFile* kw = req.hasEngineKeywords ? &req.engineKeywords : nullptr;
            auto                      vh = compute_variant_hash(meta, req.options, kw, sourceHash);
            if (!vh.isOk())
                return Result<BuildResult>::err(vh.error());
            bin.variantHash = vh.value();
        }

        // Build MaterialDescription
        MaterialDescription mdesc;
        mdesc.materialBlockName = "Material";

        auto vr = validate_and_build_mdesc(mdesc, bin.reflection, meta);
        if (!vr.isOk())
            return Result<BuildResult>::err(vr.error());

        bin.materialDesc = std::move(mdesc);

        out.binary = bin;
        out.log    = c.value().infoLog;

        if (req.enableCache)
        {
            std::filesystem::create_directories(req.cacheDir);
            const std::string path = cache_path(req.cacheDir, buildHash);
            (void)write_vshbin_file(path, bin);
        }

        return Result<BuildResult>::ok(std::move(out));
    }

    Result<ShaderBinary> build_from_spirv(const std::vector<uint32_t>& spirv, ShaderStage stage)
    {
        auto r = reflect_spirv(spirv);
        if (!r.isOk())
            return Result<ShaderBinary>::err(r.error());

        ShaderBinary bin;
        bin.stage       = stage;
        bin.spirv       = spirv;
        bin.spirvHash   = xxhash64_words(bin.spirv);
        bin.contentHash = xxhash64_words(bin.spirv);

        MaterialDescription mdesc;
        mdesc.materialBlockName = "Material";
        mdesc.renderState       = RenderState {};

        ParsedMetadata emptyMeta;
        auto           vr = validate_and_build_mdesc(mdesc, bin.reflection, emptyMeta);
        if (!vr.isOk())
        {
            // When building from raw SPIR-V, lack of metadata is acceptable.
            // We still require a Material block for param schema.
            // If this is too strict for some workflows, we can expose a lower-level reflection-only path.
            return Result<ShaderBinary>::err(vr.error());
        }

        bin.reflection   = std::move(r.value());
        bin.materialDesc = std::move(mdesc);

        return Result<ShaderBinary>::ok(std::move(bin));
    }
} // namespace vshadersystem
