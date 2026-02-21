#include "vshadersystem/metadata.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

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

    Result<ParsedMetadata> parse_vultra_metadata(std::string_view sourceText)
    {
        ParsedMetadata out;

        std::string line;
        line.reserve(512);

        size_t i = 0;
        while (i < sourceText.size())
        {
            size_t j = sourceText.find('\n', i);
            if (j == std::string_view::npos)
                j = sourceText.size();
            std::string_view sv = sourceText.substr(i, j - i);
            i                   = (j == sourceText.size()) ? j : (j + 1);

            // Strip CR
            if (!sv.empty() && sv.back() == '\r')
                sv = sv.substr(0, sv.size() - 1);

            std::string_view s = sv;
            // Left trim
            while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
                s.remove_prefix(1);

            if (!starts_with(s, "#pragma vultra"))
                continue;

            // Tokenize by spaces (attributes stay as a single token because they contain parentheses, no spaces
            // inside).
            std::vector<std::string_view> toks;
            {
                size_t k = 0;
                while (k < s.size())
                {
                    while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k])))
                        ++k;
                    if (k >= s.size())
                        break;
                    size_t k2 = k;
                    while (k2 < s.size() && !std::isspace(static_cast<unsigned char>(s[k2])))
                        ++k2;
                    toks.push_back(s.substr(k, k2 - k));
                    k = k2;
                }
            }

            if (toks.size() < 3)
                return Result<ParsedMetadata>::err(
                    {ErrorCode::eParseError, "Invalid #pragma vultra line (too few tokens)."});

            // toks[0] = #pragma, toks[1] = vultra, toks[2] = keyword
            const auto keyword = toks[2];

            if (keyword == "material")
            {
                out.hasMaterialDecl = true;
                continue;
            }
            else if (keyword == "param")
            {
                if (toks.size() < 4)
                    return Result<ParsedMetadata>::err(
                        {ErrorCode::eParseError, "param pragma requires a parameter name."});

                std::string name(toks[3]);
                auto&       meta = out.params[name];

                for (size_t t = 4; t < toks.size(); ++t)
                {
                    std::string_view payload;

                    if (parse_attr(toks[t], "semantic", payload))
                    {
                        Semantic sem = Semantic::eUnknown;
                        if (!parse_semantic(payload, sem))
                            return Result<ParsedMetadata>::err(
                                {ErrorCode::eParseError, "Unknown semantic: " + std::string(payload)});
                        meta.semantic = sem;
                        continue;
                    }

                    if (parse_attr(toks[t], "default", payload))
                    {
                        std::vector<float> values;
                        if (!parse_parenthesized_list(std::string("(") + std::string(payload) + ")", values))
                        {
                            // Accept "default(1,2,3)" where payload is "1,2,3" (already without parentheses by
                            // parse_attr). We re-wrap to reuse parser.
                            values.clear();
                            std::string wrapped = "(" + std::string(payload) + ")";
                            if (!parse_parenthesized_list(wrapped, values))
                                return Result<ParsedMetadata>::err(
                                    {ErrorCode::eParseError, "Invalid default(...) list."});
                        }
                        meta.hasDefault = true;

                        write_default(meta.defaultValue, values);
                        continue;
                    }

                    if (parse_attr(toks[t], "range", payload))
                    {
                        std::vector<float> values;
                        std::string        wrapped = "(" + std::string(payload) + ")";
                        if (!parse_parenthesized_list(wrapped, values) || values.size() != 2)
                            return Result<ParsedMetadata>::err(
                                {ErrorCode::eParseError, "range(min,max) expects exactly two numbers."});
                        meta.hasRange  = true;
                        meta.range.min = values[0];
                        meta.range.max = values[1];
                        continue;
                    }

                    return Result<ParsedMetadata>::err(
                        {ErrorCode::eParseError, "Unknown param attribute token: " + std::string(toks[t])});
                }
            }
            else if (keyword == "texture")
            {
                if (toks.size() < 4)
                    return Result<ParsedMetadata>::err(
                        {ErrorCode::eParseError, "texture pragma requires a texture name."});

                std::string name(toks[3]);
                auto&       meta = out.textures[name];

                for (size_t t = 4; t < toks.size(); ++t)
                {
                    std::string_view payload;

                    if (parse_attr(toks[t], "semantic", payload))
                    {
                        Semantic sem = Semantic::eUnknown;
                        if (!parse_semantic(payload, sem))
                            return Result<ParsedMetadata>::err(
                                {ErrorCode::eParseError, "Unknown semantic: " + std::string(payload)});
                        meta.semantic = sem;
                        continue;
                    }

                    return Result<ParsedMetadata>::err(
                        {ErrorCode::eParseError, "Unknown texture attribute token: " + std::string(toks[t])});
                }
            }
            else if (keyword == "render")
            {
                // v1: opaque/transparent only; renderer maps it to queues
                // We store this indirectly via blend/depth hints and future flags.
                out.renderStateExplicit = true;
                // Accept token but no storage yet; kept for future extension.
                continue;
            }
            else if (keyword == "state")
            {
                auto subKeyword = toks[3];
                if (subKeyword == "Blend")
                {
                    if (toks.size() < 6)
                        return Result<ParsedMetadata>::err({ErrorCode::eParseError, "Blend requires src dst"});

                    BlendFactor src;
                    BlendFactor dst;

                    if (!parse_blend_factor(toks[4], src))
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Unknown blend source factor: " + std::string(toks[4])});

                    if (!parse_blend_factor(toks[5], dst))
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Unknown blend destination factor: " + std::string(toks[5])});

                    out.renderState.blendEnable = true;
                    out.renderState.srcColor    = src;
                    out.renderState.dstColor    = dst;
                    out.renderState.srcAlpha    = src;
                    out.renderState.dstAlpha    = dst;

                    out.renderStateExplicit = true;
                }
                else if (subKeyword == "BlendOp")
                {
                    if (toks.size() < 6)
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "BlendOp requires colorOp alphaOp"});

                    BlendOp colorOp;
                    BlendOp alphaOp;

                    if (!parse_blend_op(toks[4], colorOp))
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Unknown blend color operation: " + std::string(toks[4])});

                    if (!parse_blend_op(toks[5], alphaOp))
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Unknown blend alpha operation: " + std::string(toks[5])});

                    out.renderState.blendEnable = true;
                    out.renderState.colorOp     = colorOp;
                    out.renderState.alphaOp     = alphaOp;

                    out.renderStateExplicit = true;
                }
                else if (subKeyword == "ZTest")
                {
                    if (toks.size() < 5)
                        return Result<ParsedMetadata>::err({ErrorCode::eParseError, "ZTest pragma requires On|Off"});
                    bool v = true;
                    if (!parse_bool_token(toks[4], v))
                        return Result<ParsedMetadata>::err({ErrorCode::eParseError, "ZTest expects On|Off"});
                    out.renderState.depthTest = v;
                    out.renderStateExplicit   = true;
                }
                else if (subKeyword == "ZWrite")
                {
                    if (toks.size() < 5)
                        return Result<ParsedMetadata>::err({ErrorCode::eParseError, "ZWrite pragma requires On|Off"});
                    bool v = true;
                    if (!parse_bool_token(toks[4], v))
                        return Result<ParsedMetadata>::err({ErrorCode::eParseError, "ZWrite expects On|Off"});
                    out.renderState.depthWrite = v;
                    out.renderStateExplicit    = true;
                }
                else if (subKeyword == "CompareOp")
                {
                    if (toks.size() < 5)
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "CompareOp pragma requires a comparison function"});
                    CompareOp f;
                    if (!parse_compare_op(toks[4], f))
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Unknown compare op: " + std::string(toks[4])});
                    out.renderState.depthFunc = f;
                    out.renderStateExplicit   = true;
                }
                else if (subKeyword == "Cull")
                {
                    if (toks.size() < 5)
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Cull pragma requires None|Back|Front"});
                    CullMode c;
                    if (!parse_cull(toks[4], c))
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Unknown cull mode: " + std::string(toks[4])});
                    out.renderState.cull    = c;
                    out.renderStateExplicit = true;
                }
                else if (subKeyword == "AlphaToCoverage")
                {
                    if (toks.size() < 5)
                        return Result<ParsedMetadata>::err({ErrorCode::eParseError, "AlphaToCoverage requires On|Off"});
                    bool v = true;
                    if (!parse_bool_token(toks[4], v))
                        return Result<ParsedMetadata>::err({ErrorCode::eParseError, "AlphaToCoverage expects On|Off"});
                    out.renderState.alphaToCoverage = v;
                    out.renderStateExplicit         = true;
                }
                else if (subKeyword == "ColorMask")
                {
                    if (toks.size() < 5)
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "ColorMask requires a combination of R,G,B,A"});
                    uint8_t mask = 0;
                    for (char c : toks[4])
                    {
                        switch (c)
                        {
                            case 'R':
                                mask |= static_cast<uint8_t>(ColorMaskFlagBits::eColorMaskR);
                                break;
                            case 'G':
                                mask |= static_cast<uint8_t>(ColorMaskFlagBits::eColorMaskG);
                                break;
                            case 'B':
                                mask |= static_cast<uint8_t>(ColorMaskFlagBits::eColorMaskB);
                                break;
                            case 'A':
                                mask |= static_cast<uint8_t>(ColorMaskFlagBits::eColorMaskA);
                                break;
                            default:
                                return Result<ParsedMetadata>::err(
                                    {ErrorCode::eParseError, "Unknown color mask character: " + std::string(1, c)});
                        }
                    }
                    out.renderState.colorMask = mask;
                    out.renderStateExplicit   = true;
                }
                else if (subKeyword == "DepthBias")
                {
                    if (toks.size() < 6)
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "DepthBias requires two float values: factor and units"});

                    float factor = 0.0f;
                    float units  = 0.0f;

                    if (!parse_float(toks[4], factor))
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Invalid DepthBias factor value: " + std::string(toks[4])});

                    if (!parse_float(toks[5], units))
                        return Result<ParsedMetadata>::err(
                            {ErrorCode::eParseError, "Invalid DepthBias units value: " + std::string(toks[5])});

                    out.renderState.depthBiasFactor = factor;
                    out.renderState.depthBiasUnits  = units;
                    out.renderStateExplicit         = true;
                }
            }
            else
            {
                return Result<ParsedMetadata>::err(
                    {ErrorCode::eParseError, "Unknown #pragma vultra keyword: " + std::string(keyword)});
            }
        }

        return Result<ParsedMetadata>::ok(std::move(out));
    }
} // namespace vshadersystem
