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
#include <cstring> // strlen
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
            case ShaderStage::eGeom:
                return meta.entryGeom;
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
    // INI-style shader stage extraction
    //  - Keeps only code inside stage sections.
    //  - Ignores non-code sections like [vshader]/[properties]/[renderstate]/[keywords].
    //  - Optional shared section: [shared] or [common]
    //
    // Motivation:
    //   INI blocks are not valid GLSL, so we must NOT forward them to the GLSL compiler.
    // ============================================================

    struct IniStageExtraction
    {
        std::string shared;
        std::string stage;
    };

    static bool ini_section_to_stage(std::string_view sectionLower, ShaderStage& out)
    {
        // accept both legacy and v0.5+ names
        if (sectionLower == "vert" || sectionLower == "vertex")
        {
            out = ShaderStage::eVert;
            return true;
        }
        if (sectionLower == "frag" || sectionLower == "fragment")
        {
            out = ShaderStage::eFrag;
            return true;
        }
        if (sectionLower == "geom" || sectionLower == "geometry")
        {
            out = ShaderStage::eGeom;
            return true;
        }
        if (sectionLower == "comp" || sectionLower == "compute")
        {
            out = ShaderStage::eComp;
            return true;
        }
        if (sectionLower == "task")
        {
            out = ShaderStage::eTask;
            return true;
        }
        if (sectionLower == "mesh")
        {
            out = ShaderStage::eMesh;
            return true;
        }
        if (sectionLower == "rgen" || sectionLower == "raygen")
        {
            out = ShaderStage::eRgen;
            return true;
        }
        if (sectionLower == "rmiss" || sectionLower == "miss" || sectionLower == "raymiss")
        {
            out = ShaderStage::eRmiss;
            return true;
        }
        if (sectionLower == "rchit" || sectionLower == "closesthit" || sectionLower == "raychit")
        {
            out = ShaderStage::eRchit;
            return true;
        }
        if (sectionLower == "rahit" || sectionLower == "anyhit" || sectionLower == "rayahit")
        {
            out = ShaderStage::eRahit;
            return true;
        }
        if (sectionLower == "rint" || sectionLower == "intersect" || sectionLower == "rayint")
        {
            out = ShaderStage::eRint;
            return true;
        }
        return false;
    }

    static inline std::string to_lower_copy(std::string_view sv)
    {
        std::string out;
        out.resize(sv.size());
        for (size_t i = 0; i < sv.size(); ++i)
            out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(sv[i])));
        return out;
    }

    static Result<IniStageExtraction> extract_ini_stage_source(const std::string& src, ShaderStage wantStage)
    {
        IniStageExtraction out;

        enum class Target
        {
            eNone,
            eShared,
            eStage
        };

        Target curTarget = Target::eNone;

        size_t i = 0;
        while (i < src.size())
        {
            const size_t lineStart = i;

            size_t lineEnd = src.find('\n', i);
            if (lineEnd == std::string::npos)
                lineEnd = src.size();
            else
                lineEnd++;

            std::string_view line(src.data() + lineStart, lineEnd - lineStart);
            std::string_view t = trim(line);

            // tolerate BOM on first line
            if (lineStart == 0 && !t.empty() && static_cast<unsigned char>(t.front()) == 0xEF)
            {
                constexpr std::string_view bom("\xEF\xBB\xBF", 3);
                if (t.size() >= bom.size() && t.substr(0, bom.size()) == bom)
                    t.remove_prefix(bom.size());
                t = trim(t);
            }

            if (t.size() >= 3 && t.front() == '[' && t.back() == ']')
            {
                std::string secLower = to_lower_copy(t.substr(1, t.size() - 2));

                if (secLower == "shared" || secLower == "common")
                {
                    curTarget = Target::eShared;
                }
                else
                {
                    ShaderStage secStage {};
                    if (ini_section_to_stage(secLower, secStage))
                    {
                        curTarget = (secStage == wantStage) ? Target::eStage : Target::eNone;
                    }
                    else
                    {
                        curTarget = Target::eNone;
                    }
                }

                i = lineEnd;
                continue;
            }

            if (curTarget == Target::eShared)
                out.shared.append(line);
            else if (curTarget == Target::eStage)
                out.stage.append(line);

            i = lineEnd;
        }

        if (out.stage.empty())
        {
            return Result<IniStageExtraction>::err(
                {ErrorCode::eParseError, "INI-style shader: requested stage section is missing"});
        }

        return Result<IniStageExtraction>::ok(std::move(out));
    }

    // Split GLSL source into (directivePrefix, rest).
    // directivePrefix includes leading empty lines and any lines starting with '#'
    // (e.g. #version, #extension, #define) until the first non-directive line.
    static inline void split_glsl_directive_prefix(const std::string& src, std::string& outPrefix, std::string& outRest)
    {
        outPrefix.clear();
        outRest.clear();

        size_t i      = 0;
        bool   sawAny = false;

        while (i < src.size())
        {
            const size_t lineStart = i;
            size_t       lineEnd   = src.find('\n', i);
            if (lineEnd == std::string::npos)
                lineEnd = src.size();
            else
                lineEnd++;

            std::string_view line(src.data() + lineStart, lineEnd - lineStart);
            std::string_view t = trim(line);

            if (!sawAny && t.empty())
            {
                outPrefix.append(line);
                i = lineEnd;
                continue;
            }

            sawAny = true;

            if (!t.empty() && t.front() == '#')
            {
                outPrefix.append(line);
                i = lineEnd;
                continue;
            }

            // first non-directive
            outRest.assign(src.begin() + static_cast<std::ptrdiff_t>(lineStart), src.end());
            return;
        }

        // src contained only directives/whitespace
        outRest.clear();
    }

    // Ensure GLSL has a leading #version directive.
    // INI-style shaders typically specify version in [vshader] instead of writing #version per-stage.
    static inline std::string ensure_glsl_version(std::string src, uint32_t version)
    {
        // Find first non-empty, non-//comment line.
        size_t i = 0;
        while (i < src.size())
        {
            size_t lineEnd = src.find('\n', i);
            if (lineEnd == std::string::npos)
                lineEnd = src.size();

            std::string_view line(src.data() + i, lineEnd - i);
            std::string_view t = trim(line);

            if (t.empty())
            {
                i = (lineEnd == src.size()) ? lineEnd : (lineEnd + 1);
                continue;
            }

            if (t.size() >= 2 && t[0] == '/' && t[1] == '/')
            {
                i = (lineEnd == src.size()) ? lineEnd : (lineEnd + 1);
                continue;
            }

            if (t.rfind("#version", 0) == 0)
                return src;

            break;
        }

        return "#version " + std::to_string(version) + "\n" + src;
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
        // compile profile / target settings affect emitted SPIR-V
        h = xxhash64(&opt.materialAccessMode, sizeof(opt.materialAccessMode), h);
        h = xxhash64(&opt.spirvVersion, sizeof(opt.spirvVersion), h);
        h = xxhash64(&opt.webgpuProfile, sizeof(opt.webgpuProfile), h);
        h = xxhash64(&opt.optimize, sizeof(opt.optimize), h);
        h = xxhash64(&opt.debugInfo, sizeof(opt.debugInfo), h);
        h = xxhash64(&opt.stripDebugInfo, sizeof(opt.stripDebugInfo), h);

        // material injection affects compilation (preamble + helper macros)
        if (opt.materialInjection.has_value())
        {
            const auto& inj = opt.materialInjection.value();
            h               = xxhash64(std::string("materialInjection=1"), h);
            h               = xxhash64(inj.preamble, h);
            h               = xxhash64(inj.postMaterialDecl, h);
            h               = xxhash64(inj.materialAddressExpr, h);
            h               = xxhash64(inj.materialIndexExpr, h);
            h               = xxhash64(inj.bindlessTextureArrayName, h);
            h               = xxhash64(inj.macroPrefix, h);
        }
        else
        {
            h = xxhash64(std::string("materialInjection=0"), h);
        }

        auto defs = normalize_define_list(opt.defines);
        h         = xxhash64(defs, h);

        for (const auto& d : opt.includeDirs)
            h = xxhash64(d, h);

        for (const auto& file : opt.virtualIncludeFiles)
        {
            h = xxhash64(file.virtualPath, h);
            h = xxhash64(file.sourceText, h);
        }

        // metadata normalization (what impacts .vshbin content)
        {
            std::string m;
            m.reserve(512);

            m += "lang=" + std::to_string(static_cast<int>(meta.language)) + "\n";
            m += "queue=" + std::to_string(meta.renderQueue) + "\n";

            m += meta.hasMaterialDecl ? "material=1\n" : "material=0\n";
            m += "materialStruct=" + meta.materialStructName + "\n";

            if (meta.isIniStyle)
            {
                // ini-style shaders may auto-generate code that affects compilation
                m += "ini=1\n";
                m += "preamble=" + meta.generatedPreamble + "\n";
            }
            else
            {
                m += "ini=0\n";
            }

            m += "entryVert=" + meta.entryVert + "\n";
            m += "entryFrag=" + meta.entryFrag + "\n";
            m += "entryGeom=" + meta.entryGeom + "\n";
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
                    m += "p:" + k + ":hasType=" + std::to_string(pm.hasType) + "\n";
                    m += "p:" + k + ":type=" + std::to_string(static_cast<uint32_t>(pm.type)) + "\n";

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

                    for (const auto& option : pm.enumOptions)
                    {
                        m += "p:" + k + ":enum=" + option.label + "=" + std::to_string(option.value) + "\n";
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
    // Material injection helpers (engine-agnostic contract)
    // ============================================================

    static Result<std::string> make_material_injection_helpers(const CompileOptions& opt, ShaderLanguage lang)
    {
        if (!opt.materialInjection.has_value())
            return Result<std::string>::ok({});

        const auto& inj    = opt.materialInjection.value();
        std::string prefix = inj.macroPrefix.empty() ? "VSH_" : inj.macroPrefix;

        // v0.5+ (injection-only):
        //   - vshadersystem never declares descriptor layouts / push constants for material access.
        //   - The caller provides resources + a matching vshader_LoadMaterial(...) overload via preamble/includes.
        //   - We only generate convenience wrappers/macros based on the provided expressions.
        std::string out;
        out.reserve(1024);
        out += "\n// vshadersystem material injection helpers\n";

        const bool isGlsl = (lang == ShaderLanguage::eGLSL || lang == ShaderLanguage::eAuto);

        // Optional: auto-generate a minimal, engine-agnostic LoadMaterial for BDA.
        // This avoids any set/binding conventions and only relies on Vulkan GLSL buffer_reference.
        if (opt.materialAccessMode == MaterialAccessMode::eBDA)
        {
            if (isGlsl)
            {
                out += "#extension GL_EXT_buffer_reference2 : require\n";
                out += "#extension GL_EXT_scalar_block_layout : require\n";
                out += "#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require\n\n";
                out +=
                    "layout(buffer_reference, scalar) readonly buffer vshader_MaterialRef { Material material; };\n\n";
                out +=
                    "Material vshader_LoadMaterial(uint64_t addr) { return vshader_MaterialRef(addr).material; }\n\n";
            }
        }

        if (!inj.postMaterialDecl.empty())
        {
            out += inj.postMaterialDecl;
            if (!out.empty() && out.back() != '\n')
                out += "\n";
            out += "\n";
        }

        // Address/index accessors (choose what the caller provided)
        if (!inj.materialAddressExpr.empty())
        {
            if (isGlsl)
                out +=
                    "uint64_t " + prefix + "MATERIAL_ADDRESS() { return uint64_t(" + inj.materialAddressExpr + "); }\n";
            else
                out += "uint64_t " + prefix + "MATERIAL_ADDRESS() { return (uint64_t)(" + inj.materialAddressExpr +
                       "); }\n";

            out += "#define " + prefix + "MATERIAL() vshader_LoadMaterial(" + prefix + "MATERIAL_ADDRESS())\n";
        }
        else if (!inj.materialIndexExpr.empty())
        {
            out += "uint " + prefix + "MATERIAL_INDEX() { return uint(" + inj.materialIndexExpr + "); }\n";
            out += "#define " + prefix + "MATERIAL() vshader_LoadMaterial(" + prefix + "MATERIAL_INDEX())\n";
        }
        else
        {
            // Default:
            //  - For BDA, the shader should call vshader_LoadMaterial(addr) directly.
            //  - For other modes, expect a no-arg vshader_LoadMaterial() provided by the caller.
            if (opt.materialAccessMode != MaterialAccessMode::eBDA)
                out += "#define " + prefix + "MATERIAL() vshader_LoadMaterial()\n";
        }

        // Optional bindless sampling helpers
        if (!inj.bindlessTextureArrayName.empty())
        {
            // Note: requires GL_EXT_nonuniform_qualifier in GLSL; caller should provide in preamble if needed.
            out += "#define " + prefix + "TEX2D(idx) " + inj.bindlessTextureArrayName + "[nonuniformEXT(uint(idx))]\n";
            out += "#define " + prefix + "SAMPLE2D(idx, uv) texture(" + prefix + "TEX2D(idx), (uv))\n";
        }

        out += "\n";
        return Result<std::string>::ok(out);
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
    //  - When a Material struct exists in shader source but there is NO reflected
    //    uniform/storage/push block to use, parse the GLSL `struct` and compute
    //    offsets/sizes (scalar layout).
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
                if (it->second.hasType)
                    pd.type = it->second.type;
                pd.enumOptions = it->second.enumOptions;

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

    static Result<void>
    validate_and_build_mdesc(MaterialDescription&  mdesc,
                             const ShaderReflection& refl,
                             const ParsedMetadata&   meta,
                             const std::string&      materialSource)
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

                // INI v0.5: texture properties are stored as *_index members inside Material.
                // Build params/textures from reflected Material block members.
                mdesc.params.clear();
                mdesc.textures.clear();
                mdesc.params.reserve(matBlock->members.size());
                mdesc.textures.reserve(matBlock->members.size());

                for (const auto& mem : matBlock->members)
                {
                    // Texture index convention
                    if (mem.name.ends_with("_index"))
                    {
                        std::string baseName = mem.name.substr(0, mem.name.size() - std::strlen("_index"));

                        MaterialTextureDesc td;
                        td.name    = baseName;
                        td.type    = TextureType::eUnknown;
                        td.set     = 0;
                        td.binding = 0;
                        td.count   = 1;

                        const auto it = meta.textures.find(baseName);
                        if (it != meta.textures.end())
                            td.semantic = it->second.semantic;

                        mdesc.textures.push_back(std::move(td));
                        continue;
                    }

                    MaterialParamDesc pd;
                    pd.name   = mem.name;
                    pd.offset = mem.offset;
                    pd.size   = mem.size;
                    pd.type   = mem.type;

                    const auto it = meta.params.find(mem.name);
                    if (it != meta.params.end())
                    {
                        pd.semantic = it->second.semantic;
                        if (it->second.hasType)
                            pd.type = it->second.type;
                        pd.enumOptions = it->second.enumOptions;

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

                mdesc.renderState = meta.renderState;
            }
            else
            {
                auto parsed = build_mdesc_from_struct_scalar_layout(materialSource, blockName, mdesc, meta);
                if (!parsed.isOk())
                    return parsed;
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

        return Result<void>::ok();
    }

    // ============================================================
    // build a single shader stage
    // ============================================================

    Result<BuildResult> build_single_shader(const BuildRequest& req)
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

        if (lang == ShaderLanguage::eAuto)
            lang = ShaderLanguage::eGLSL;

        // Per-stage entry (GLSL uses a generated wrapper when needed)
        CompileOptions opt = req.options;
        opt.language       = lang;

        opt.entryPoint = entry_for_stage(meta, opt.stage);

        // Shader identity is the explicit id (required). It comes from the
        // BuildRequest override (for raw sources) or the INI-style `[vshader] id`.
        // The legacy filename-stem derivation has been removed: ids must be stable
        // and author-controlled (e.g. "builtin/fxaa"), independent of file location.
        const std::string shaderId = !req.id.empty() ? req.id : meta.id;
        if (shaderId.empty())
            return Result<BuildResult>::err(
                {ErrorCode::eParseError,
                 "shader '" + req.source.virtualPath +
                     "' is missing a required id (set BuildRequest.id or [vshader] id = \"builtin/fxaa\")"});

        const uint64_t buildHash    = compute_build_hash(req.source, opt, meta);
        const uint64_t sourceHash   = xxhash64(req.source.sourceText);
        const uint64_t shaderIdHash = shader_id_hash(shaderId);

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
        // INI stage extraction
        //   - v0.5.0 uses INI-style shaders exclusively.
        //   - Extract the shared header + requested stage section.
        // ====================================================

        {
            auto ex = extract_ini_stage_source(req.source.sourceText, opt.stage);
            if (!ex.isOk())
                return Result<BuildResult>::err(ex.error());

            // Assemble extracted stage source.
            // v0.5+ INI shaders do NOT require authors to write #version per-stage.
            // We always ensure a leading #version directive for GLSL.
            std::string assembled = std::move(ex.value().shared);
            if (!assembled.empty() && !assembled.ends_with("\n"))
                assembled.push_back('\n');
            assembled += std::move(ex.value().stage);

            if (lang == ShaderLanguage::eGLSL)
                assembled = ensure_glsl_version(std::move(assembled), meta.glslVersion);

            // Deterministic preamble placement:
            //  - keep directive prefix (#version/#extension/#define...) at the top
            //  - then inject: (user injection preamble) followed by (auto-generated preamble)
            //  - then the rest of the stage code
            std::string combinedPreamble;
            if (opt.materialInjection.has_value() && !opt.materialInjection->preamble.empty())
                combinedPreamble += opt.materialInjection->preamble;
            if (!meta.generatedPreamble.empty())
            {
                if (!combinedPreamble.empty())
                    combinedPreamble += "\n";
                combinedPreamble += meta.generatedPreamble;
            }

            if (lang == ShaderLanguage::eGLSL && !combinedPreamble.empty())
            {
                std::string dir;
                std::string rest;
                split_glsl_directive_prefix(assembled, dir, rest);
                stageSrc.sourceText = std::move(dir);
                if (!stageSrc.sourceText.empty() && !stageSrc.sourceText.ends_with("\n"))
                    stageSrc.sourceText.push_back('\n');
                stageSrc.sourceText += combinedPreamble;
                if (!stageSrc.sourceText.ends_with("\n"))
                    stageSrc.sourceText.push_back('\n');
                stageSrc.sourceText += std::move(rest);
            }
            else
                stageSrc.sourceText = std::move(assembled);
        }

        // ====================================================
        // Material injection
        // ====================================================

        if (meta.hasMaterialDecl)
        {
            auto helperR = make_material_injection_helpers(opt, lang);
            if (!helperR.isOk())
                return Result<BuildResult>::err(helperR.error());

            stageSrc.sourceText = backends.injector->inject(
                stageSrc.sourceText, meta.materialStructName, opt.materialAccessMode, helperR.value());
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

        // The fallback parser needs the final per-stage source, because INI [properties]
        // generates the Material struct during stage assembly.
        auto vr = validate_and_build_mdesc(mdesc, bin.reflection, meta, stageSrc.sourceText);

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

    static bool ini_has_stage_section(const std::string& src, ShaderStage wantStage)
    {
        size_t i = 0;
        while (i < src.size())
        {
            const size_t lineStart = i;
            size_t       lineEnd   = src.find('\n', i);
            if (lineEnd == std::string::npos)
                lineEnd = src.size();
            else
                lineEnd++;

            std::string_view line(src.data() + lineStart, lineEnd - lineStart);
            std::string_view t = trim(line);

            // tolerate BOM on first line
            if (lineStart == 0 && !t.empty() && static_cast<unsigned char>(t.front()) == 0xEF)
            {
                constexpr std::string_view bom("\xEF\xBB\xBF", 3);
                if (t.size() >= bom.size() && t.substr(0, bom.size()) == bom)
                    t.remove_prefix(bom.size());
                t = trim(t);
            }

            if (t.size() >= 3 && t.front() == '[' && t.back() == ']')
            {
                std::string secLower = to_lower_copy(t.substr(1, t.size() - 2));

                ShaderStage secStage {};
                if (ini_section_to_stage(secLower, secStage) && secStage == wantStage)
                    return true;
            }

            i = lineEnd;
        }

        return false;
    }

    // ============================================================
    // build multiple stages from single source (GLSL multi-stage shader)
    //  - look for stage markers like `[vert]`, `[frag]` in source text
    //  - if found, build that stage with filtered source; else skip stage
    //  - if no stage markers found at all, return error
    // ============================================================
    Result<std::unordered_map<ShaderStage, BuildResult>> build_multiple_shaders(const BuildRequest& req)
    {
        std::unordered_map<ShaderStage, BuildResult> out;

        const ShaderStage stages[] = {
            ShaderStage::eVert,
            ShaderStage::eFrag,
            ShaderStage::eGeom,
            ShaderStage::eComp,
            ShaderStage::eTask,
            ShaderStage::eMesh,
        };

        bool foundAny = false;

        for (const auto& s : stages)
        {
            if (!ini_has_stage_section(req.source.sourceText, s))
                continue;

            foundAny = true;

            BuildRequest singleReq  = req;
            singleReq.options.stage = s;
            auto r                  = build_single_shader(singleReq);

            if (!r.isOk())
                return Result<std::unordered_map<ShaderStage, BuildResult>>::err(r.error());

            out[s] = std::move(r.value());
        }

        if (!foundAny)
        {
            return Result<std::unordered_map<ShaderStage, BuildResult>>::err(
                {ErrorCode::eParseError, "INI-style shader: no stage sections found"});
        }

        return Result<std::unordered_map<ShaderStage, BuildResult>>::ok(std::move(out));
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
        auto vr = validate_and_build_mdesc(mdesc, r.value(), emptyMeta, {});

        if (!vr.isOk())
            return Result<ShaderBinary>::err(vr.error());

        bin.reflection   = std::move(r.value());
        bin.materialDesc = std::move(mdesc);

        return Result<ShaderBinary>::ok(std::move(bin));
    }

} // namespace vshadersystem
