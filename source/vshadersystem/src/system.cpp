#include "vshadersystem/system.hpp"
#include "vshadersystem/backends.hpp"
#include "vshadersystem/binary.hpp"
#include "vshadersystem/compiler.hpp"
#include "vshadersystem/hash.hpp"
#include "vshadersystem/metadata.hpp"
#include "vshadersystem/parser_utils.hpp"
#include "vshadersystem/reflect.hpp"
#include "vshadersystem/shader_id.hpp"
#include "vshadersystem/variant_key.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vshadersystem
{
    // ============================================================
    // small helpers
    // ============================================================

    static inline std::string normalize_define_list(const std::vector<Define>& defs)
    {
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

    static inline std::string_view trim(std::string_view s)
    {
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0))
            s.remove_prefix(1);

        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0))
            s.remove_suffix(1);

        return s;
    }

    static inline const std::string& entry_for_stage(const ParsedMetadata& meta, ShaderStage stage)
    {
        switch (stage)
        {
            case ShaderStage::eVert:
                return meta.entryVert;
            case ShaderStage::eFrag:
                return meta.entryFrag;
            case ShaderStage::eComp:
                return meta.entryComp;
            case ShaderStage::eTask:
                return meta.entryTask;
            case ShaderStage::eMesh:
                return meta.entryMesh;
            default:
                break;
        }

        static const std::string kMain = "main";
        return kMain;
    }

    // ============================================================
    // GLSL single-file stage filter
    //  - keeps shared (before first marker)
    //  - keeps only requested stage section
    //  - drops other stage sections
    //  - marker line can have trailing stuff -> starts_with
    // ============================================================

    static const char* stage_marker(ShaderStage s)
    {
        switch (s)
        {
            case ShaderStage::eVert:
                return "[vert]";
            case ShaderStage::eFrag:
                return "[frag]";
            case ShaderStage::eComp:
                return "[comp]";
            case ShaderStage::eTask:
                return "[task]";
            case ShaderStage::eMesh:
                return "[mesh]";
            default:
                return nullptr;
        }
    }

    static Result<std::string> filter_stage_source_glsl(const std::string& src, ShaderStage stage)
    {
        const char* want = stage_marker(stage);
        if (!want)
            return Result<std::string>::err({ErrorCode::eInvalidArgument, "Invalid stage marker"});

        std::string out;

        bool inWanted  = false;
        bool sawMarker = false;

        size_t i = 0;
        while (i < src.size())
        {
            const size_t lineStart = i;

            size_t lineEnd = src.find('\n', i);
            if (lineEnd == std::string::npos)
                lineEnd = src.size();
            else
                lineEnd++;

            const std::string_view line(src.data() + lineStart, lineEnd - lineStart);
            const std::string_view t = trim(line);

            const bool isMarker = t.starts_with("[vert]") || t.starts_with("[frag]") || t.starts_with("[comp]") ||
                                  t.starts_with("[task]") || t.starts_with("[mesh]");

            if (isMarker)
            {
                sawMarker = true;
                inWanted  = t.starts_with(want);
            }
            else
            {
                if (!sawMarker || inWanted)
                    out.append(line);
            }

            i = lineEnd;
        }

        return Result<std::string>::ok(std::move(out));
    }

    // ============================================================
    // strip `#pragma vultra ...` lines before compiling GLSL
    // ============================================================

    static std::string strip_vultra_pragmas(const std::string& src)
    {
        std::string out;
        out.reserve(src.size());

        size_t i = 0;
        while (i < src.size())
        {
            size_t lineEnd = src.find('\n', i);
            if (lineEnd == std::string::npos)
                lineEnd = src.size();
            else
                lineEnd++;

            std::string_view line(src.data() + i, lineEnd - i);
            std::string_view t = trim(line);

            // tolerate BOM on first line
            if (!t.empty() && static_cast<unsigned char>(t.front()) == 0xEF)
            {
                constexpr std::string_view bom("\xEF\xBB\xBF", 3);

                if (t.size() >= bom.size() && t.substr(0, bom.size()) == bom)
                    t.remove_prefix(bom.size());

                t = trim(t);
            }

            if (!t.starts_with("#pragma vultra"))
                out.append(line);

            i = lineEnd;
        }

        return out;
    }

    // ============================================================
    // GLSL wrapper main()
    // ============================================================

    static void append_glsl_main_wrapper(std::string& src, const std::string& entry)
    {
        if (entry.empty() || entry == "main")
            return;

        src += "\n// vshadersystem wrapper\n";
        src += "void main(){ " + entry + "(); }\n";
    }

    // ============================================================
    // Build hash (full metadata hashing + entry + language)
    // ============================================================

    static uint64_t compute_build_hash(const SourceInput& src, const CompileOptions& opt, const ParsedMetadata& meta)
    {
        uint64_t h = 0;

        h = xxhash64(src.sourceText, h);
        h = xxhash64(src.virtualPath, h);

        h = xxhash64(&opt.stage, sizeof(opt.stage), h);

        // entry affects wrapper/compilation
        h = xxhash64(opt.entryPoint, h);

        // language affects backend + preprocessing
        h = xxhash64(&opt.language, sizeof(opt.language), h);

        auto defs = normalize_define_list(opt.defines);
        h         = xxhash64(defs, h);

        for (const auto& d : opt.includeDirs)
            h = xxhash64(d, h);

        // metadata normalization (what impacts .vshbin content)
        {
            std::string m;
            m.reserve(512);

            m += "lang=" + std::to_string(static_cast<int>(meta.language)) + "\n";
            m += "queue=" + std::to_string(meta.renderQueue) + "\n";

            m += meta.hasMaterialDecl ? "material=1\n" : "material=0\n";
            m += "materialStruct=" + meta.materialStructName + "\n";

            m += "entryVert=" + meta.entryVert + "\n";
            m += "entryFrag=" + meta.entryFrag + "\n";
            m += "entryComp=" + meta.entryComp + "\n";
            m += "entryTask=" + meta.entryTask + "\n";
            m += "entryMesh=" + meta.entryMesh + "\n";

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
            {
                std::vector<std::string> keys;
                keys.reserve(meta.params.size());

                for (const auto& [k, _] : meta.params)
                    keys.push_back(k);

                std::sort(keys.begin(), keys.end());

                for (const auto& k : keys)
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
                        m += "p:" + k + ":range=" + std::to_string(pm.range.min) + "," + std::to_string(pm.range.max) +
                             "\n";
                    }
                }
            }

            // textures
            {
                std::vector<std::string> keys;
                keys.reserve(meta.textures.size());

                for (const auto& [k, _] : meta.textures)
                    keys.push_back(k);

                std::sort(keys.begin(), keys.end());

                for (const auto& k : keys)
                {
                    const auto& tm = meta.textures.at(k);
                    m += "t:" + k + ":sem=" + std::to_string(static_cast<uint32_t>(tm.semantic)) + "\n";
                }
            }

            h = xxhash64(m, h);
        }

        return h;
    }

    // ============================================================
    // cache path
    // ============================================================

    static std::string cache_path(const std::string& dir, uint64_t hash)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
        return (std::filesystem::path(dir) / (std::string(buf) + ".vshbin")).string();
    }

    // ============================================================
    // Material struct parser fallback (GLSL scalar block layout)
    //
    // Goal:
    //  - When `#pragma vultra material <StructName>` is present but there is NO
    //    reflected uniform/storage/push block to use, we parse the GLSL `struct`
    //    and compute offsets/sizes ourselves (scalar layout).
    //
    // Supported:
    //  - scalar: int/uint/float/bool + explicit arithmetic types int8/16/64, float16
    //  - vectors: vec{2,3,4}, ivec/uvec/bvec, i16vec/u16vec, i64vec/u64vec, f16vec
    //  - fixed arrays: `T name[N];` (single-dimension)
    //
    // Limitations (safe-by-default):
    //  - nested structs: rejected
    //  - matrices: rejected
    // ============================================================

    static uint32_t align_up_u32(uint32_t v, uint32_t a)
    {
        if (a == 0u)
            return v;

        const uint32_t mask = a - 1u;
        return (v + mask) & ~mask;
    }

    struct ScalarTypeInfo
    {
        ParamType type      = ParamType::eFloat;
        uint32_t  elemSize  = 0; // bytes per scalar element
        uint32_t  elemAlign = 0; // scalar alignment
        uint32_t  vecSize   = 1; // 1..4
        bool      ok        = false;
    };

    static ScalarTypeInfo parse_glsl_scalar_or_vector_type(std::string_view t)
    {
        // canonicalize: remove spaces
        t = trim(t);

        auto make = [](ParamType pt, uint32_t es, uint32_t ea, uint32_t vs) -> ScalarTypeInfo {
            ScalarTypeInfo out;
            out.type      = pt;
            out.elemSize  = es;
            out.elemAlign = ea;
            out.vecSize   = vs;
            out.ok        = true;
            return out;
        };

        // --- float vectors ---
        if (t == "float")
            return make(ParamType::eFloat, 4, 4, 1);
        if (t == "vec2")
            return make(ParamType::eVec2, 4, 4, 2);
        if (t == "vec3")
            return make(ParamType::eVec3, 4, 4, 3);
        if (t == "vec4")
            return make(ParamType::eVec4, 4, 4, 4);

        // --- int vectors ---
        if (t == "int")
            return make(ParamType::eInt, 4, 4, 1);

        // --- uint vectors ---
        if (t == "uint")
            return make(ParamType::eUInt, 4, 4, 1);

        // --- bool vectors (treated as 32-bit in Vulkan ABI) ---
        if (t == "bool")
            return make(ParamType::eBool, 4, 4, 1);

        return {};
    }

    static std::string strip_line_comment(std::string_view line)
    {
        const size_t p = line.find("//");
        if (p == std::string::npos)
            return std::string(line);

        return std::string(line.substr(0, p));
    }

    struct ParsedField
    {
        std::string typeName;
        std::string name;
        uint32_t    arrayCount = 0; // 0 => not array
    };

    static bool parse_struct_field_line(std::string_view line, ParsedField& out)
    {
        // expected forms (with optional qualifiers we ignore):
        //   <type> <name>;
        //   <type> <name>[N];
        // also tolerate: `layout(...) <type> <name>;` inside struct (rare, but ignore)
        line = trim(line);

        if (line.empty())
            return false;

        // ignore braces
        if (line.starts_with("{") || line.starts_with("}"))
            return false;

        // remove trailing ';'
        if (line.back() == ';')
            line.remove_suffix(1);

        // drop any leading qualifiers like "const", "precise", "volatile" (minimal)
        auto drop_leading_word = [&](std::string_view w) {
            if (line.starts_with(w))
            {
                line.remove_prefix(w.size());
                line = trim(line);
            }
        };

        drop_leading_word("const");
        drop_leading_word("precise");
        drop_leading_word("volatile");
        drop_leading_word("coherent");
        drop_leading_word("readonly");
        drop_leading_word("writeonly");

        // Now first token is type
        const size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos)
            return false;

        std::string_view typeTok = trim(line.substr(0, sp));
        std::string_view rest    = trim(line.substr(sp + 1));

        if (typeTok.empty() || rest.empty())
            return false;

        // name possibly includes [N]
        std::string nameStr(rest);

        // trim possible initializer: `x = ...`
        const size_t eq = nameStr.find('=');
        if (eq != std::string::npos)
            nameStr = nameStr.substr(0, eq);

        // remove trailing spaces
        while (!nameStr.empty() && (std::isspace(static_cast<unsigned char>(nameStr.back())) != 0))
            nameStr.pop_back();

        // parse array
        uint32_t     arrayCount = 0;
        const size_t lb         = nameStr.find('[');
        if (lb != std::string::npos)
        {
            const size_t rb = nameStr.find(']', lb);
            if (rb == std::string::npos)
                return false;

            const std::string_view nsv(nameStr.data() + lb + 1, rb - lb - 1);
            std::string_view       nt = trim(nsv);
            if (nt.empty())
                return false;

            uint64_t v = 0;
            for (char c : nt)
            {
                if ((c < '0') || (c > '9'))
                    return false;
                v = v * 10u + static_cast<uint64_t>(c - '0');
                if (v > 0xFFFFFFFFu)
                    return false;
            }
            arrayCount = static_cast<uint32_t>(v);

            nameStr = nameStr.substr(0, lb);
        }

        // remove trailing spaces again
        while (!nameStr.empty() && (std::isspace(static_cast<unsigned char>(nameStr.back())) != 0))
            nameStr.pop_back();

        if (nameStr.empty())
            return false;

        out.typeName   = std::string(typeTok);
        out.name       = nameStr;
        out.arrayCount = arrayCount;
        return true;
    }

    static Result<void> build_mdesc_from_struct_scalar_layout(const std::string&    sourceText,
                                                              const std::string&    structName,
                                                              MaterialDescription&  mdesc,
                                                              const ParsedMetadata& meta)
    {
        if (structName.empty())
        {
            return Result<void>::err({ErrorCode::eParseError, "Material struct name is empty"});
        }

        // locate `struct <Name>`
        const std::string key = "struct " + structName;
        const size_t      p0  = sourceText.find(key);

        if (p0 == std::string::npos)
        {
            return Result<void>::err({ErrorCode::eParseError, "Material struct not found: " + structName});
        }

        const size_t brace0 = sourceText.find('{', p0);
        if (brace0 == std::string::npos)
        {
            return Result<void>::err({ErrorCode::eParseError, "Material struct missing '{': " + structName});
        }

        // find matching closing brace
        int    depth = 1;
        size_t i     = brace0 + 1;

        size_t brace1 = std::string::npos;
        for (; i < sourceText.size(); ++i)
        {
            const char c = sourceText[i];

            if (c == '{')
                depth++;
            else if (c == '}')
            {
                depth--;
                if (depth == 0)
                {
                    brace1 = i;
                    break;
                }
            }
        }

        if (brace1 == std::string::npos)
        {
            return Result<void>::err({ErrorCode::eParseError, "Material struct missing '}' : " + structName});
        }

        // Extract body
        const std::string_view body(sourceText.data() + brace0 + 1, brace1 - brace0 - 1);

        uint32_t offset = 0;

        mdesc.params.clear();
        mdesc.textures.clear(); // textures still come from reflection descriptors; keep empty here
        mdesc.renderState = meta.renderState;

        // parse line-by-line
        size_t li = 0;
        while (li < body.size())
        {
            size_t le = body.find('\n', li);
            if (le == std::string::npos)
                le = body.size();
            else
                le++;

            std::string_view line(body.data() + li, le - li);
            li = le;

            std::string      lineNoComment = strip_line_comment(line);
            std::string_view t             = trim(std::string_view(lineNoComment));

            if (t.empty())
                continue;

            // reject nested struct quickly
            if (t.starts_with("struct "))
            {
                return Result<void>::err(
                    {ErrorCode::eParseError, "Nested structs in Material are not supported (yet)."});
            }

            ParsedField fld;
            if (!parse_struct_field_line(t, fld))
                continue;

            const ScalarTypeInfo ti = parse_glsl_scalar_or_vector_type(fld.typeName);

            if (!ti.ok)
            {
                return Result<void>::err({ErrorCode::eParseError, "Unsupported Material field type: " + fld.typeName});
            }

            // scalar-layout:
            // - base alignment = scalar alignment
            // - vec size = elemSize * vecSize
            const uint32_t elemAlign = ti.elemAlign;
            const uint32_t elemSize  = ti.elemSize;
            const uint32_t vecSize   = ti.vecSize;

            const uint32_t fieldAlign = elemAlign;
            const uint32_t fieldSize  = elemSize * vecSize;

            const uint32_t count     = (fld.arrayCount == 0u) ? 1u : fld.arrayCount;
            const uint32_t totalSize = fieldSize * count;

            offset = align_up_u32(offset, fieldAlign);

            MaterialParamDesc pd;
            pd.name   = fld.name;
            pd.offset = offset;
            pd.size   = totalSize;
            pd.type   = ti.type;

            const auto it = meta.params.find(pd.name);
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

            offset += totalSize;
        }

        mdesc.materialParamSize = offset;

        // strict validation: metadata params must exist in parsed struct
        for (const auto& [name, _] : meta.params)
        {
            bool found = false;

            for (const auto& p : mdesc.params)
            {
                if (p.name == name)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                return Result<void>::err(
                    {ErrorCode::eParseError, "Metadata param '" + name + "' not found in Material struct members."});
            }
        }

        return Result<void>::ok();
    }

    // ============================================================
    // MaterialDescription builder
    //  - If hasMaterialDecl:
    //      * if block exists => use reflection block
    //      * else parse struct and compute offsets (scalar layout)
    //  - If no hasMaterialDecl:
    //      * use block if exists (default "Material"), else allow empty
    // ============================================================

    static Result<void> validate_and_build_mdesc(MaterialDescription&    mdesc,
                                                 const ShaderReflection& refl,
                                                 const ParsedMetadata&   meta,
                                                 const std::string&      sourceText)
    {
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

        // ------------------------------------------------------------
        // Case A: has material decl
        // ------------------------------------------------------------
        if (meta.hasMaterialDecl)
        {
            if (matBlock != nullptr)
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

                    const auto it = meta.params.find(mem.name);
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

                // Textures: from descriptors (sampled image / combined)
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
                    td.type    = TextureType::eUnknown;

                    const auto it = meta.textures.find(d.name);
                    if (it != meta.textures.end())
                        td.semantic = it->second.semantic;

                    mdesc.textures.push_back(std::move(td));
                }

                mdesc.renderState = meta.renderState;

                // Strict validation: metadata params must exist in block
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
                    {
                        return Result<void>::err(
                            {ErrorCode::eParseError,
                             "Metadata param '" + name + "' not found in Material block members."});
                    }
                }
            }
            else
            {
                // No block -> parse struct and compute scalar-layout offsets
                if (meta.materialStructName.empty())
                {
                    // `#pragma vultra material` without name: we cannot parse a struct schema.
                    // Treat as empty schema.
                    mdesc.materialParamSize = 0;
                    mdesc.params.clear();
                    mdesc.textures.clear();
                    mdesc.renderState = meta.renderState;
                }
                else
                {
                    auto r = build_mdesc_from_struct_scalar_layout(sourceText, meta.materialStructName, mdesc, meta);
                    if (!r.isOk())
                        return r;
                }
            }
        }
        // ------------------------------------------------------------
        // Case B: no material decl
        // ------------------------------------------------------------
        else
        {
            if (matBlock != nullptr)
            {
                mdesc.materialParamSize = matBlock->size;

                mdesc.params.clear();
                mdesc.params.reserve(matBlock->members.size());

                for (const auto& mem : matBlock->members)
                {
                    MaterialParamDesc pd;
                    pd.name   = mem.name;
                    pd.offset = mem.offset;
                    pd.size   = mem.size;
                    pd.type   = mem.type;
                    mdesc.params.push_back(std::move(pd));
                }
            }
            else
            {
                mdesc.materialParamSize = 0;
                mdesc.params.clear();
            }

            // textures still from descriptors
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
                td.type    = TextureType::eUnknown;
                mdesc.textures.push_back(std::move(td));
            }

            mdesc.renderState = meta.renderState;
        }

        // Strict validation: metadata textures must exist in reflected descriptors
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
            {
                return Result<void>::err(
                    {ErrorCode::eParseError, "Metadata texture '" + name + "' not found in reflected descriptors."});
            }
        }

        return Result<void>::ok();
    }

    // ============================================================
    // build_shader (FULL)
    // ============================================================

    Result<BuildResult> build_shader(const BuildRequest& req)
    {
        // Parse metadata first (also goes into cache key)
        auto metaR = parse_vultra_metadata(req.source.sourceText);

        if (!metaR.isOk())
            return Result<BuildResult>::err(metaR.error());

        ParsedMetadata meta = std::move(metaR.value());

        // Decide language (options override pragma)
        ShaderLanguage lang = req.options.language;

        if (lang == ShaderLanguage::eAuto)
            lang = meta.language;

        // Per-stage entry (GLSL uses wrapper; Slang can ignore but keep consistent hashing)
        CompileOptions opt = req.options;
        opt.language       = lang;

        if (lang == ShaderLanguage::eGLSL)
            opt.entryPoint = entry_for_stage(meta, opt.stage);
        else
            opt.entryPoint.clear();

        const uint64_t buildHash    = compute_build_hash(req.source, opt, meta);
        const uint64_t sourceHash   = xxhash64(req.source.sourceText);
        const uint64_t shaderIdHash = shader_id_hash_from_virtual_path(req.source.virtualPath);

        BuildResult out;
        out.fromCache = false;

        // Cache load
        if (req.enableCache)
        {
            const std::string path = cache_path(req.cacheDir, buildHash);

            auto cached = read_vshbin_file(path);

            if (cached.isOk())
            {
                out.binary    = std::move(cached.value());
                out.fromCache = true;
                out.log       = "Cache hit: " + path;

                return Result<BuildResult>::ok(std::move(out));
            }
        }

        BackendBundle backends = create_backends(lang);

        SourceInput stageSrc = req.source;

        // ====================================================
        // GLSL: stage filtering + strip tool pragmas + wrapper
        // Slang: keep original source
        // ====================================================

        if (lang == ShaderLanguage::eGLSL)
        {
            auto filtered = filter_stage_source_glsl(req.source.sourceText, opt.stage);

            if (!filtered.isOk())
                return Result<BuildResult>::err(filtered.error());

            stageSrc.sourceText = std::move(filtered.value());

            // remove tool pragmas before compile
            stageSrc.sourceText = strip_vultra_pragmas(stageSrc.sourceText);
        }
        else
        {
            stageSrc.sourceText = req.source.sourceText;
        }

        // ====================================================
        // Material injection
        // ====================================================

        if (meta.hasMaterialDecl)
        {
            stageSrc.sourceText =
                backends.injector->inject(stageSrc.sourceText, meta.materialStructName, opt.materialAccessMode);
        }

        // ====================================================
        // GLSL wrapper
        // ====================================================

        if (lang == ShaderLanguage::eGLSL)
            append_glsl_main_wrapper(stageSrc.sourceText, opt.entryPoint);

        // ====================================================
        // Compile
        // ====================================================

        Result<CompileOutput> comp = backends.compiler->compile(stageSrc, opt);

        if (!comp.isOk())
            return Result<BuildResult>::err(comp.error());

        // ====================================================
        // Reflect
        // ====================================================

        auto reflR = backends.reflector->reflect(comp.value().spirv, {}, &comp.value());

        if (!reflR.isOk())
            return Result<BuildResult>::err(reflR.error());

        ShaderBinary bin;
        bin.stage        = opt.stage;
        bin.spirv        = std::move(comp.value().spirv);
        bin.spirvHash    = xxhash64_words(bin.spirv);
        bin.contentHash  = sourceHash;
        bin.shaderIdHash = shaderIdHash;
        bin.reflection   = std::move(reflR.value());

        // ====================================================
        // Variant hash (permutation keywords only)
        // ====================================================

        {
            VariantKey key;

            key.setShaderIdHash(shaderIdHash);
            key.setStage(opt.stage);

            const EngineKeywordsFile* kw = req.hasEngineKeywords ? &req.engineKeywords : nullptr;

            for (const auto& kd : meta.keywords)
            {
                if (kd.dispatch != KeywordDispatch::ePermutation)
                    continue;

                uint32_t value = kd.defaultValue;

                // override from -D
                for (const auto& d : opt.defines)
                {
                    if (d.name == kd.name)
                    {
                        auto pv = parse_keyword_value(kd, d.value);

                        if (!pv.isOk())
                            return Result<BuildResult>::err(pv.error());

                        value = pv.value();
                        break;
                    }
                }

                // override from engine keywords
                if (kw != nullptr)
                {
                    auto it = kw->values.find(kd.name);

                    if (it != kw->values.end())
                    {
                        auto pv = parse_keyword_value(kd, it->second);

                        if (!pv.isOk())
                            return Result<BuildResult>::err(pv.error());

                        value = pv.value();
                    }
                }

                key.set(kd.name, value);
            }

            bin.variantHash = key.build();
        }

        // ====================================================
        // MaterialDescription
        // ====================================================

        MaterialDescription mdesc;

        // If pragma provides struct name, use that as schema name; else default block name is "Material"
        if (meta.hasMaterialDecl && !meta.materialStructName.empty())
            mdesc.materialBlockName = meta.materialStructName;
        else
            mdesc.materialBlockName = "Material";

        // IMPORTANT: for struct-fallback parsing we must parse from the ORIGINAL file source,
        // not the filtered/stripped/injected per-stage text.
        auto vr = validate_and_build_mdesc(mdesc, bin.reflection, meta, req.source.sourceText);

        if (!vr.isOk())
            return Result<BuildResult>::err(vr.error());

        bin.materialDesc = std::move(mdesc);

        out.binary    = bin;
        out.fromCache = false;
        out.log       = comp.value().infoLog;

        // Cache write
        if (req.enableCache)
        {
            std::filesystem::create_directories(req.cacheDir);

            const std::string path = cache_path(req.cacheDir, buildHash);

            (void)write_vshbin_file(path, bin);
        }

        return Result<BuildResult>::ok(std::move(out));
    }

    // ============================================================
    // build_from_spirv (compat)
    // ============================================================

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

        // No metadata -> minimal materialDesc
        MaterialDescription mdesc;
        mdesc.materialBlockName = "Material";
        mdesc.renderState       = RenderState {};

        ParsedMetadata emptyMeta;

        // When building from raw SPIR-V we cannot parse GLSL structs from text.
        // Keep block-based behavior only.
        auto vr = validate_and_build_mdesc(mdesc, r.value(), emptyMeta, std::string());

        if (!vr.isOk())
            return Result<ShaderBinary>::err(vr.error());

        bin.reflection   = std::move(r.value());
        bin.materialDesc = std::move(mdesc);

        return Result<ShaderBinary>::ok(std::move(bin));
    }

} // namespace vshadersystem