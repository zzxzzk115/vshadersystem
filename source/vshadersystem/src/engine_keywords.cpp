#include "vshadersystem/engine_keywords.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace vshadersystem
{
    static inline void trim_inplace(std::string& s)
    {
        size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
            ++a;
        size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
            --b;
        s = s.substr(a, b - a);
    }

    static bool parse_dispatch(std::string_view s, KeywordDispatch& out)
    {
        if (s == "permute")
        {
            out = KeywordDispatch::ePermutation;
            return true;
        }
        if (s == "runtime")
        {
            out = KeywordDispatch::eRuntime;
            return true;
        }
        if (s == "special")
        {
            out = KeywordDispatch::eSpecialization;
            return true;
        }
        return false;
    }

    static bool parse_scope(std::string_view s, KeywordScope& out)
    {
        if (s == "global")
        {
            out = KeywordScope::eGlobal;
            return true;
        }
        if (s == "material")
        {
            out = KeywordScope::eMaterial;
            return true;
        }
        if (s == "pass")
        {
            out = KeywordScope::ePass;
            return true;
        }
        if (s == "local" || s == "shader" || s == "shaderlocal")
        {
            out = KeywordScope::eShaderLocal;
            return true;
        }
        return false;
    }

    static Result<KeywordDecl> parse_keyword_decl_tokens(const std::vector<std::string_view>& toks)
    {
        // toks: ["keyword", dispatch, (optional scope), nameOrNameEq]
        if (toks.size() < 3)
            return Result<KeywordDecl>::err({ErrorCode::eParseError, "vkw: keyword line too short."});

        KeywordDecl decl;

        if (!parse_dispatch(toks[1], decl.dispatch))
            return Result<KeywordDecl>::err({ErrorCode::eParseError, "vkw: unknown dispatch: " + std::string(toks[1])});

        size_t       idx = 2;
        KeywordScope sc;
        if (idx < toks.size() && parse_scope(toks[idx], sc))
        {
            decl.scope = sc;
            ++idx;
        }

        if (idx >= toks.size())
            return Result<KeywordDecl>::err({ErrorCode::eParseError, "vkw: keyword requires a name."});

        std::string_view nameToken = toks[idx++];
        std::string_view namePart  = nameToken;
        std::string_view rhsPart   = {};
        const auto       eqPos     = nameToken.find('=');
        if (eqPos != std::string_view::npos)
        {
            namePart = nameToken.substr(0, eqPos);
            rhsPart  = nameToken.substr(eqPos + 1);
        }
        decl.name = std::string(namePart);

        if (!rhsPart.empty())
        {
            if (rhsPart == "0" || rhsPart == "1")
            {
                decl.kind         = KeywordValueKind::eBool;
                decl.defaultValue = (rhsPart == "1") ? 1u : 0u;
            }
            else
            {
                decl.kind = KeywordValueKind::eEnum;

                size_t p0 = 0;
                while (p0 <= rhsPart.size())
                {
                    auto p1 = rhsPart.find('|', p0);
                    if (p1 == std::string_view::npos)
                        p1 = rhsPart.size();
                    auto item = rhsPart.substr(p0, p1 - p0);
                    if (!item.empty())
                        decl.enumValues.emplace_back(item);
                    if (p1 == rhsPart.size())
                        break;
                    p0 = p1 + 1;
                }

                if (decl.enumValues.empty())
                    return Result<KeywordDecl>::err({ErrorCode::eParseError, "vkw: enum keyword has no enumerants."});
                decl.defaultValue = 0;
            }
        }

        return Result<KeywordDecl>::ok(std::move(decl));
    }

    Result<EngineKeywordsFile> parse_engine_keywords_vkw(std::string_view text)
    {
        EngineKeywordsFile out;

        std::istringstream iss((std::string(text)));
        std::string        line;
        size_t             lineNo = 0;

        while (std::getline(iss, line))
        {
            ++lineNo;
            trim_inplace(line);
            if (line.empty() || line[0] == '#')
                continue;

            // tokenize by whitespace
            std::vector<std::string_view> toks;
            {
                std::string_view s(line);
                size_t           k = 0;
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
            if (toks.empty())
                continue;

            if (toks[0] == "keyword")
            {
                auto r = parse_keyword_decl_tokens(toks);
                if (!r.isOk())
                {
                    return Result<EngineKeywordsFile>::err(
                        {ErrorCode::eParseError, "vkw line " + std::to_string(lineNo) + ": " + r.error().message});
                }
                out.decls.push_back(std::move(r.value()));
            }
            else if (toks[0] == "set")
            {
                if (toks.size() < 2)
                    return Result<EngineKeywordsFile>::err(
                        {ErrorCode::eParseError, "vkw line " + std::to_string(lineNo) + ": set requires NAME=VALUE"});

                std::string_view nv = toks[1];
                auto             p  = nv.find('=');
                if (p == std::string_view::npos)
                    return Result<EngineKeywordsFile>::err(
                        {ErrorCode::eParseError, "vkw line " + std::to_string(lineNo) + ": set requires NAME=VALUE"});

                std::string name(nv.substr(0, p));
                std::string val(nv.substr(p + 1));
                out.values[std::move(name)] = std::move(val);
            }
            else
            {
                return Result<EngineKeywordsFile>::err(
                    {ErrorCode::eParseError,
                     "vkw line " + std::to_string(lineNo) + ": unknown directive: " + std::string(toks[0])});
            }
        }

        return Result<EngineKeywordsFile>::ok(std::move(out));
    }

    Result<EngineKeywordsFile> load_engine_keywords_vkw(const std::string& filePath)
    {
        std::ifstream f(filePath, std::ios::binary);
        if (!f)
            return Result<EngineKeywordsFile>::err({ErrorCode::eIO, "Failed to open vkw file: " + filePath});

        f.seekg(0, std::ios::end);
        const auto size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::string text;
        text.resize(size);
        f.read(text.data(), static_cast<std::streamsize>(size));
        if (!f)
            return Result<EngineKeywordsFile>::err({ErrorCode::eIO, "Failed to read vkw file: " + filePath});

        return parse_engine_keywords_vkw(text);
    }
} // namespace vshadersystem
