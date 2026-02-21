#include "vshadersystem/system.hpp"
#include "vshadersystem/binary.hpp"
#include "vshadersystem/compiler.hpp"
#include "vshadersystem/hash.hpp"
#include "vshadersystem/metadata.hpp"
#include "vshadersystem/reflect.hpp"

#include <algorithm>
#include <filesystem>

namespace vshadersystem
{
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
            return Result<void>::err(
                {ErrorCode::eReflectError, "Material block '" + blockName + "' not found in reflection."});

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
                    {ErrorCode::eParseError, "Metadata param '" + name + "' not found in Material block members."});
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

        const uint64_t buildHash = compute_build_hash(req.source, req.options, meta);

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
        bin.contentHash = buildHash;
        bin.reflection  = std::move(r.value());

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
