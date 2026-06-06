#include "vshadersystem/metadata.hpp"
#include "vshadersystem/keywords.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>

namespace vshadersystem
{
    static inline std::string trim(std::string s)
    {
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
            s.erase(s.begin());
        while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
            s.pop_back();
        return s;
    }

    static inline bool starts_with(std::string_view s, std::string_view p)
    {
        return s.size() >= p.size() && s.substr(0, p.size()) == p;
    }

    static inline bool parse_bool_token(std::string_view tok, bool& out)
    {
        if (tok == "On")
        {
            out = true;
            return true;
        }
        if (tok == "Off")
        {
            out = false;
            return true;
        }
        return false;
    }

    static inline bool is_number(std::string_view s)
    {
        if (s.empty())
            return false;
        for (char c : s)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    }

    static inline bool parse_semantic(std::string_view s, Semantic& out)
    {
        // We keep names stable and human-readable.
        if (s == "BaseColor")
        {
            out = Semantic::eBaseColor;
            return true;
        }
        if (s == "Metallic")
        {
            out = Semantic::eMetallic;
            return true;
        }
        if (s == "Roughness")
        {
            out = Semantic::eRoughness;
            return true;
        }
        if (s == "Normal")
        {
            out = Semantic::eNormal;
            return true;
        }
        if (s == "Emissive")
        {
            out = Semantic::eEmissive;
            return true;
        }
        if (s == "Occlusion")
        {
            out = Semantic::eOcclusion;
            return true;
        }
        if (s == "Opacity")
        {
            out = Semantic::eOpacity;
            return true;
        }
        if (s == "AlphaClip")
        {
            out = Semantic::eAlphaClip;
            return true;
        }
        if (s == "Custom")
        {
            out = Semantic::eCustom;
            return true;
        }
        if (s == "Unknown")
        {
            out = Semantic::eUnknown;
            return true;
        }
        return false;
    }

    static bool parse_blend_factor(std::string_view s, BlendFactor& out)
    {
        if (s == "One")
        {
            out = BlendFactor::eOne;
            return true;
        }
        if (s == "Zero")
        {
            out = BlendFactor::eZero;
            return true;
        }

        if (s == "SrcAlpha")
        {
            out = BlendFactor::eSrcAlpha;
            return true;
        }
        if (s == "OneMinusSrcAlpha")
        {
            out = BlendFactor::eOneMinusSrcAlpha;
            return true;
        }

        if (s == "DstAlpha")
        {
            out = BlendFactor::eDstAlpha;
            return true;
        }
        if (s == "OneMinusDstAlpha")
        {
            out = BlendFactor::eOneMinusDstAlpha;
            return true;
        }

        if (s == "SrcColor")
        {
            out = BlendFactor::eSrcColor;
            return true;
        }
        if (s == "OneMinusSrcColor")
        {
            out = BlendFactor::eOneMinusSrcColor;
            return true;
        }

        if (s == "DstColor")
        {
            out = BlendFactor::eDstColor;
            return true;
        }
        if (s == "OneMinusDstColor")
        {
            out = BlendFactor::eOneMinusDstColor;
            return true;
        }

        return false;
    }

    static inline bool parse_cull(std::string_view s, CullMode& out)
    {
        if (s == "None")
        {
            out = CullMode::eNone;
            return true;
        }
        if (s == "Back")
        {
            out = CullMode::eBack;
            return true;
        }
        if (s == "Front")
        {
            out = CullMode::eFront;
            return true;
        }
        return false;
    }

    static inline bool parse_blend_op(std::string_view s, BlendOp& out)
    {
        if (s == "Add")
        {
            out = BlendOp::eAdd;
            return true;
        }
        if (s == "Subtract")
        {
            out = BlendOp::eSubtract;
            return true;
        }
        if (s == "ReverseSubtract")
        {
            out = BlendOp::eReverseSubtract;
            return true;
        }
        if (s == "Min")
        {
            out = BlendOp::eMin;
            return true;
        }
        if (s == "Max")
        {
            out = BlendOp::eMax;
            return true;
        }
        return false;
    }

    static inline bool parse_compare_op(std::string_view s, CompareOp& out)
    {
        if (s == "Never")
        {
            out = CompareOp::eNever;
            return true;
        }
        if (s == "Less")
        {
            out = CompareOp::eLess;
            return true;
        }
        if (s == "Equal")
        {
            out = CompareOp::eEqual;
            return true;
        }
        if (s == "LessOrEqual")
        {
            out = CompareOp::eLessOrEqual;
            return true;
        }
        if (s == "Greater")
        {
            out = CompareOp::eGreater;
            return true;
        }
        if (s == "NotEqual")
        {
            out = CompareOp::eNotEqual;
            return true;
        }
        if (s == "GreaterOrEqual")
        {
            out = CompareOp::eGreaterOrEqual;
            return true;
        }
        if (s == "Always")
        {
            out = CompareOp::eAlways;
            return true;
        }
        return false;
    }

    static inline bool parse_float(std::string_view s, float& out)
    {
        // We parse with std::from_chars for locale-free behavior.
        const char* b = s.data();
        const char* e = s.data() + s.size();
        // from_chars for double is C++17 but some stdlibs are partial; we fall back to strtod.
        // We keep a simple fallback here.
        try
        {
            out = std::stof(std::string(s));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static inline bool parse_parenthesized_list(std::string_view s, std::vector<float>& out)
    {
        // expects "(...)" with comma-separated numbers
        if (s.size() < 2 || s.front() != '(' || s.back() != ')')
            return false;

        std::string       inner(s.substr(1, s.size() - 2));
        std::stringstream ss(inner);
        std::string       item;
        while (std::getline(ss, item, ','))
        {
            item = trim(item);
            if (item.empty())
                return false;
            float v = 0.0f;
            if (!parse_float(item, v))
                return false;
            out.push_back(v);
        }
        return !out.empty();
    }

    static inline bool parse_attr(std::string_view token, std::string_view name, std::string_view& payload)
    {
        // token: name(...)
        if (!starts_with(token, name))
            return false;
        if (token.size() <= name.size() || token[name.size()] != '(' || token.back() != ')')
            return false;
        payload = token.substr(name.size() + 1, token.size() - name.size() - 2);
        return true;
    }

    void write_default(ParamDefault& dst, const std::vector<float>& values)
    {
        // DO NOT touch dst.type here
        // type will be determined later by reflection

        std::memset(dst.valueBuffer, 0, sizeof(dst.valueBuffer));

        const size_t count = std::min(values.size(), static_cast<size_t>(16));

        float temp[16] = {};

        for (size_t i = 0; i < count; ++i)
            temp[i] = values[i];

        std::memcpy(dst.valueBuffer, temp, count * sizeof(float));
    }

    // ============================================================
    // v0.5.0 INI-style shader header parsing
    //
    // Supported blocks:
    //   [vshader]
    //   [properties]
    //   [keywords]
    //   [renderstate]
    //
    // Stages are handled by system.cpp via markers.
    // ============================================================

    static inline bool is_ini_style_shader(std::string_view src)
    {
        size_t i = 0;
        while (i < src.size())
        {
            size_t j = src.find('\n', i);
            if (j == std::string_view::npos)
                j = src.size();

            std::string_view line = src.substr(i, j - i);
            i                     = (j == src.size()) ? j : (j + 1);

            if (!line.empty() && line.back() == '\r')
                line = line.substr(0, line.size() - 1);

            auto t = line;
            while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
                t.remove_prefix(1);
            while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
                t.remove_suffix(1);

            if (t.empty())
                continue;
            if (starts_with(t, "//") || starts_with(t, ";"))
                continue;

            return t == "[vshader]";
        }
        return false;
    }

    static inline std::string to_lower(std::string s)
    {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static inline std::string_view strip_inline_comment(std::string_view s)
    {
        // remove // comment when not in quotes (we don't support quotes here)
        const size_t p = s.find("//");
        if (p != std::string_view::npos)
            s = s.substr(0, p);
        // remove ';' style comments as whole-line; inline ';' is allowed in GLSL so ignore.
        return s;
    }

    static inline bool parse_on_off(std::string_view v, bool& outBool)
    {
        std::string vv(v);
        vv = trim(vv);
        vv = to_lower(vv);
        if (vv == "on" || vv == "true" || vv == "1")
        {
            outBool = true;
            return true;
        }
        if (vv == "off" || vv == "false" || vv == "0")
        {
            outBool = false;
            return true;
        }
        return false;
    }

    static inline bool parse_compare_op_ini(std::string_view v, CompareOp& outOp)
    {
        std::string vv = to_lower(trim(std::string(v)));
        if (vv == "less")
        {
            outOp = CompareOp::eLess;
            return true;
        }
        if (vv == "lequal" || vv == "less_equal" || vv == "lessorequal")
        {
            outOp = CompareOp::eLessOrEqual;
            return true;
        }
        if (vv == "greater")
        {
            outOp = CompareOp::eGreater;
            return true;
        }
        if (vv == "gequal" || vv == "greater_equal" || vv == "greaterorequal")
        {
            outOp = CompareOp::eGreaterOrEqual;
            return true;
        }
        if (vv == "equal")
        {
            outOp = CompareOp::eEqual;
            return true;
        }
        if (vv == "always")
        {
            outOp = CompareOp::eAlways;
            return true;
        }
        if (vv == "never")
        {
            outOp = CompareOp::eNever;
            return true;
        }
        return false;
    }

    static inline bool parse_cull_ini(std::string_view v, CullMode& outCull)
    {
        std::string vv = to_lower(trim(std::string(v)));
        if (vv == "none" || vv == "off")
        {
            outCull = CullMode::eNone;
            return true;
        }
        if (vv == "front")
        {
            outCull = CullMode::eFront;
            return true;
        }
        if (vv == "back")
        {
            outCull = CullMode::eBack;
            return true;
        }
        return false;
    }

    static inline bool
    parse_property_line(std::string_view line, std::string& outName, std::string& outType, std::string& outDefaultExpr)
    {
        // name : type = default
        // name : Texture2D
        outName.clear();
        outType.clear();
        outDefaultExpr.clear();

        std::string s(trim(std::string(line)));
        if (s.empty())
            return false;
        if (!s.empty() && s.back() == ';')
            s.pop_back();

        const size_t colon = s.find(':');
        if (colon == std::string::npos)
            return false;

        outName          = trim(s.substr(0, colon));
        std::string rest = trim(s.substr(colon + 1));

        size_t eq    = std::string::npos;
        int    depth = 0;
        for (size_t i = 0; i < rest.size(); ++i)
        {
            if (rest[i] == '(')
                ++depth;
            else if (rest[i] == ')' && depth > 0)
                --depth;
            else if (rest[i] == '=' && depth == 0)
            {
                eq = i;
                break;
            }
        }

        if (eq == std::string::npos)
        {
            outType = trim(rest);
            return !outName.empty() && !outType.empty();
        }

        outType        = trim(rest.substr(0, eq));
        outDefaultExpr = trim(rest.substr(eq + 1));
        return !outName.empty() && !outType.empty();
    }

    static inline std::string glsl_type_from_property_type(const std::string& tyLower)
    {
        if (tyLower == "float")
            return "float";
        if (tyLower == "int")
            return "int";
        if (tyLower == "uint")
            return "uint";
        if (tyLower == "bool")
            return "bool";
        if (tyLower == "vec2")
            return "vec2";
        if (tyLower == "vec3")
            return "vec3";
        if (tyLower == "vec4")
            return "vec4";
        if (tyLower == "mat3")
            return "mat3";
        if (tyLower == "mat4")
            return "mat4";
        if (starts_with(tyLower, "enum"))
            return "int";
        return {};
    }

    static inline bool is_texture_type(const std::string& tyLower) { return starts_with(tyLower, "texture"); }

    static inline bool param_type_from_property_type(const std::string& tyLower, ParamType& out)
    {
        if (tyLower == "float")
        {
            out = ParamType::eFloat;
            return true;
        }
        if (tyLower == "int" || starts_with(tyLower, "enum"))
        {
            out = ParamType::eInt;
            return true;
        }
        if (tyLower == "uint")
        {
            out = ParamType::eUInt;
            return true;
        }
        if (tyLower == "bool")
        {
            out = ParamType::eBool;
            return true;
        }
        if (tyLower == "vec2")
        {
            out = ParamType::eVec2;
            return true;
        }
        if (tyLower == "vec3")
        {
            out = ParamType::eVec3;
            return true;
        }
        if (tyLower == "vec4")
        {
            out = ParamType::eVec4;
            return true;
        }
        if (tyLower == "mat3")
        {
            out = ParamType::eMat3;
            return true;
        }
        if (tyLower == "mat4")
        {
            out = ParamType::eMat4;
            return true;
        }
        return false;
    }

    static std::vector<MaterialParamDesc::EnumOption> parse_enum_options(std::string_view typeText)
    {
        std::vector<MaterialParamDesc::EnumOption> out;
        std::string ty = to_lower(trim(std::string(typeText)));
        if (!starts_with(ty, "enum"))
            return out;

        const size_t lp = ty.find('(');
        const size_t rp = ty.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp <= lp + 1)
            return out;

        std::string       inner = ty.substr(lp + 1, rp - lp - 1);
        std::stringstream ss(inner);
        std::string       item;
        int32_t           nextValue = 0;
        while (std::getline(ss, item, ','))
        {
            item = trim(item);
            if (item.empty())
                continue;

            MaterialParamDesc::EnumOption option;
            const size_t eq = item.find('=');
            if (eq == std::string::npos)
            {
                option.label = item;
                option.value = nextValue;
            }
            else
            {
                option.label = trim(item.substr(0, eq));
                const auto valueText = trim(item.substr(eq + 1));
                try
                {
                    option.value = static_cast<int32_t>(std::stoi(valueText));
                }
                catch (...)
                {
                    option.value = nextValue;
                }
            }
            nextValue = option.value + 1;
            out.push_back(std::move(option));
        }
        return out;
    }

    static bool parse_int_default(std::string_view                                      text,
                                  const std::vector<MaterialParamDesc::EnumOption>& enumOptions,
                                  int32_t&                                           out)
    {
        std::string value = to_lower(trim(std::string(text)));
        for (const auto& option : enumOptions)
        {
            if (to_lower(option.label) == value)
            {
                out = option.value;
                return true;
            }
        }
        try
        {
            out = static_cast<int32_t>(std::stoi(value));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static Result<ParsedMetadata> parse_ini_metadata(std::string_view sourceText)
    {
        ParsedMetadata out;
        out.isIniStyle = true;

        std::string curSection;

        // For Material auto-generation
        struct PropDecl
        {
            std::string name;
            std::string typeLower;
            std::string glslType;
            bool        isTexture = false;

            // default for non-texture
            ParamType type = ParamType::eFloat;
            std::vector<MaterialParamDesc::EnumOption> enumOptions;
        };

        std::vector<PropDecl> props;
        props.reserve(64);

        auto flush_preamble = [&]() {
            if (props.empty())
                return;

            out.hasMaterialDecl    = true;
            out.materialStructName = "Material";

            std::string code;
            code += "// ============================================================\n";
            code += "// vshadersystem v0.5: auto-generated Material struct\n";
            code += "// ============================================================\n";
            code += "struct " + out.materialStructName + "\n";
            code += "{\n";
            for (const auto& p : props)
            {
                if (p.isTexture)
                {
                    // Texture properties are represented as an integer index.
                    // The sampling/binding model is engine-defined.
                    code += "    int " + p.name + "_index;\n";
                }
                else
                {
                    code += "    " + p.glslType + " " + p.name + ";\n";
                }
            }
            code += "};\n\n";

            out.generatedPreamble = std::move(code);
        };

        size_t i = 0;
        while (i < sourceText.size())
        {
            size_t j = sourceText.find('\n', i);
            if (j == std::string_view::npos)
                j = sourceText.size();

            std::string_view line = sourceText.substr(i, j - i);
            i                     = (j == sourceText.size()) ? j : (j + 1);

            if (!line.empty() && line.back() == '\r')
                line = line.substr(0, line.size() - 1);

            line = strip_inline_comment(line);

            std::string t = trim(std::string(line));
            if (t.empty())
                continue;

            // section header
            if (t.size() >= 3 && t.front() == '[' && t.back() == ']')
            {
                curSection = to_lower(t.substr(1, t.size() - 2));
                continue;
            }

            if (curSection == "vshader")
            {
                // key = value
                const size_t eq = t.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string key    = to_lower(trim(t.substr(0, eq)));
                std::string valRaw = trim(t.substr(eq + 1));
                std::string val    = to_lower(valRaw);

                if (key == "language")
                {
                    if (val == "glsl")
                        out.language = ShaderLanguage::eGLSL;
                    else if (val == "slang")
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Slang is no longer supported. Please migrate this shader to GLSL."});
                    else
                        out.language = ShaderLanguage::eAuto;
                }
                else if (key == "version" || key == "target_version")
                {
                    // GLSL version (e.g. 460). Keep permissive: digits only.
                    uint32_t v  = 0;
                    bool     ok = !valRaw.empty();
                    for (char c : valRaw)
                    {
                        if (c < '0' || c > '9')
                        {
                            ok = false;
                            break;
                        }
                        v = v * 10u + static_cast<uint32_t>(c - '0');
                    }

                    if (ok && v >= 110)
                        out.glslVersion = v;
                }
                else if (key == "material")
                {
                    out.materialStructName = trim(t.substr(eq + 1));
                    if (out.materialStructName.empty())
                        out.materialStructName = "Material";
                }
                else if (key == "render_queue" || key == "renderqueue")
                {
                    if (is_number(val))
                        out.renderQueue = static_cast<uint32_t>(std::stoul(val));
                }
                else if (key == "id")
                {
                    // Explicit, stable logical shader id, e.g. id = "builtin/fxaa".
                    // Quotes are optional; surrounding quotes are stripped.
                    std::string idVal = valRaw;
                    if (idVal.size() >= 2 && idVal.front() == '"' && idVal.back() == '"')
                        idVal = idVal.substr(1, idVal.size() - 2);
                    out.id = trim(idVal);
                }

                continue;
            }

            if (curSection == "keywords")
            {
                // NAME : <bool|enum(...)> <permute|runtime|special> [scope]
                const size_t colon = t.find(':');
                if (colon == std::string::npos)
                    continue;
                std::string name = trim(t.substr(0, colon));
                std::string rhs  = trim(t.substr(colon + 1));
                if (name.empty() || rhs.empty())
                    continue;

                std::stringstream ss(rhs);
                std::string       typeTok;
                std::string       dispatchTok;
                std::string       scopeTok;
                ss >> typeTok;
                ss >> dispatchTok;
                ss >> scopeTok;

                KeywordDecl kd;
                kd.name = name;

                // type
                std::string typeLower = to_lower(typeTok);
                if (typeLower == "bool")
                {
                    kd.kind = KeywordValueKind::eBool;
                }
                else if (starts_with(typeLower, "enum"))
                {
                    kd.kind = KeywordValueKind::eEnum;
                    // enum(a,b,c)
                    const size_t lp = typeLower.find('(');
                    const size_t rp = typeLower.find(')');
                    if (lp != std::string::npos && rp != std::string::npos && rp > lp + 1)
                    {
                        std::string       inner = typeLower.substr(lp + 1, rp - lp - 1);
                        std::stringstream ess(inner);
                        std::string       item;
                        while (std::getline(ess, item, ','))
                        {
                            item = trim(item);
                            if (!item.empty())
                                kd.enumValues.push_back(item);
                        }
                    }
                    if (kd.enumValues.empty())
                        kd.enumValues.push_back("0");
                    kd.defaultValue = 0;
                }
                else
                {
                    // fallback -> bool
                    kd.kind = KeywordValueKind::eBool;
                }

                // dispatch
                std::string dispLower = to_lower(dispatchTok);
                if (dispLower == "permute")
                    kd.dispatch = KeywordDispatch::ePermutation;
                else if (dispLower == "special")
                    kd.dispatch = KeywordDispatch::eSpecialization;
                else
                    kd.dispatch = KeywordDispatch::eRuntime;

                // scope optional
                std::string scopeLower = to_lower(scopeTok);
                if (scopeLower == "global")
                    kd.scope = KeywordScope::eGlobal;
                else if (scopeLower == "material")
                    kd.scope = KeywordScope::eMaterial;
                else if (scopeLower == "pass")
                    kd.scope = KeywordScope::ePass;
                else
                    kd.scope = KeywordScope::eShaderLocal;

                out.keywords.push_back(std::move(kd));
                continue;
            }

            if (curSection == "renderstate")
            {
                const size_t eq = t.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string key = to_lower(trim(t.substr(0, eq)));
                std::string val = trim(t.substr(eq + 1));

                out.renderStateExplicit = true;

                if (key == "cull")
                {
                    (void)parse_cull_ini(val, out.renderState.cull);
                }
                else if (key == "depth_test" || key == "depthtest")
                {
                    (void)parse_on_off(val, out.renderState.depthTest);
                }
                else if (key == "depth_write" || key == "depthwrite")
                {
                    (void)parse_on_off(val, out.renderState.depthWrite);
                }
                else if (key == "depth_func" || key == "depthfunc" || key == "depth_compare" || key == "depthcompare")
                {
                    (void)parse_compare_op_ini(val, out.renderState.depthFunc);
                }
                else if (key == "blend")
                {
                    (void)parse_on_off(val, out.renderState.blendEnable);
                }
                else if (key == "alpha_to_coverage" || key == "alphatocoverage")
                {
                    (void)parse_on_off(val, out.renderState.alphaToCoverage);
                }
                else if (key == "depth_bias" || key == "depthbias")
                {
                    // "factor units" or "(factor,units)"
                    std::vector<float> vs;
                    std::string        vv = trim(std::string(val));
                    if (!vv.empty() && vv.front() == '(')
                    {
                        if (parse_parenthesized_list(vv, vs) && vs.size() >= 2)
                        {
                            out.renderState.depthBiasFactor = vs[0];
                            out.renderState.depthBiasUnits  = vs[1];
                        }
                    }
                    else
                    {
                        std::stringstream ss(vv);
                        ss >> out.renderState.depthBiasFactor;
                        ss >> out.renderState.depthBiasUnits;
                    }
                }
                continue;
            }

            if (curSection == "properties")
            {
                std::string name;
                std::string ty;
                std::string def;
                if (!parse_property_line(t, name, ty, def))
                    continue;

                PropDecl p;
                p.name      = name;
                p.typeLower = to_lower(ty);
                p.isTexture = is_texture_type(p.typeLower);
                p.glslType  = glsl_type_from_property_type(p.typeLower);
                (void)param_type_from_property_type(p.typeLower, p.type);
                p.enumOptions = parse_enum_options(p.typeLower);

                if (!p.isTexture && p.glslType.empty())
                {
                    return Result<ParsedMetadata>::err(
                        {ErrorCode::eParseError, "Unknown property type: " + ty + " for '" + name + "'"});
                }

                // Default values
                if (!p.isTexture)
                {
                    ParsedMetadata::ParamMeta pm;
                    pm.semantic    = Semantic::eUnknown;
                    pm.hasType     = true;
                    pm.type        = p.type;
                    pm.enumOptions = p.enumOptions;
                    pm.hasDefault  = false;
                    pm.defaultValue.type = p.type;

                    if (!def.empty())
                    {
                        std::string d = trim(def);
                        if (p.type == ParamType::eInt)
                        {
                            int32_t v = 0;
                            if (!parse_int_default(d, p.enumOptions, v))
                                return Result<ParsedMetadata>::err(
                                    {ErrorCode::eParseError, "Invalid default for property '" + name + "'"});
                            std::memset(pm.defaultValue.valueBuffer, 0, sizeof(pm.defaultValue.valueBuffer));
                            std::memcpy(pm.defaultValue.valueBuffer, &v, sizeof(v));
                        }
                        else if (p.type == ParamType::eUInt)
                        {
                            uint32_t v = 0;
                            try
                            {
                                v = static_cast<uint32_t>(std::stoul(d));
                            }
                            catch (...)
                            {
                                return Result<ParsedMetadata>::err(
                                    {ErrorCode::eParseError, "Invalid default for property '" + name + "'"});
                            }
                            std::memset(pm.defaultValue.valueBuffer, 0, sizeof(pm.defaultValue.valueBuffer));
                            std::memcpy(pm.defaultValue.valueBuffer, &v, sizeof(v));
                        }
                        else if (p.type == ParamType::eBool)
                        {
                            const auto lowered = to_lower(d);
                            if (lowered != "true" && lowered != "1" && lowered != "on" && lowered != "false" &&
                                lowered != "0" && lowered != "off")
                            {
                                return Result<ParsedMetadata>::err(
                                    {ErrorCode::eParseError, "Invalid default for property '" + name + "'"});
                            }
                            const bool v = lowered == "true" || lowered == "1" || lowered == "on";
                            std::memset(pm.defaultValue.valueBuffer, 0, sizeof(pm.defaultValue.valueBuffer));
                            std::memcpy(pm.defaultValue.valueBuffer, &v, sizeof(v));
                        }
                        else
                        {
                            std::vector<float> values;
                            if (!d.empty() && d.front() != '(')
                            {
                                float v = 0.0f;
                                if (!parse_float(d, v))
                                    return Result<ParsedMetadata>::err(
                                        {ErrorCode::eParseError, "Invalid default for property '" + name + "'"});
                                values.push_back(v);
                            }
                            else
                            {
                                if (!parse_parenthesized_list(d, values))
                                    return Result<ParsedMetadata>::err(
                                        {ErrorCode::eParseError, "Invalid default for property '" + name + "'"});
                            }
                            write_default(pm.defaultValue, values);
                        }
                        pm.hasDefault = true;
                    }
                    out.params[p.name] = std::move(pm);
                }
                else
                {
                    ParsedMetadata::TextureMeta tm;
                    tm.semantic          = Semantic::eUnknown;
                    out.textures[p.name] = tm;
                }

                props.push_back(std::move(p));
                continue;
            }
        }

        flush_preamble();

        return Result<ParsedMetadata>::ok(std::move(out));
    }

    Result<ParsedMetadata> parse_vultra_metadata(std::string_view sourceText)
    {
        if (!is_ini_style_shader(sourceText))
        {
            return Result<ParsedMetadata>::err({ErrorCode::eParseError,
                                                "vshadersystem v0.5.0: legacy #pragma syntax is not supported. "
                                                "Use INI-style sections like [vshader]/[properties]/[vert]/[frag]."});
        }

        return parse_ini_metadata(sourceText);
    }
} // namespace vshadersystem
