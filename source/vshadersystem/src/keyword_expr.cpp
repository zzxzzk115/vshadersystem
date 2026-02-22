#include "vshadersystem/keyword_expr.hpp"

#include <cctype>
#include <string>

namespace vshadersystem
{
    namespace
    {
        struct Token
        {
            enum Kind
            {
                eEnd,
                eIdent,
                eNumber,
                eLParen,
                eRParen,
                eEqEq,
                eNotEq,
                eAndAnd,
                eOrOr,
            } kind = eEnd;

            std::string text;
        };

        struct Lexer
        {
            std::string_view s;
            size_t           i = 0;

            static bool isIdentStart(char c) { return (std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_'; }
            static bool isIdentChar(char c) { return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_'; }

            void skipWs()
            {
                while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) != 0))
                    ++i;
            }

            Token next()
            {
                skipWs();
                if (i >= s.size())
                    return Token {Token::eEnd, {}};

                const char c = s[i];

                // operators / parens
                if (c == '(')
                {
                    ++i;
                    return Token {Token::eLParen, "("};
                }
                if (c == ')')
                {
                    ++i;
                    return Token {Token::eRParen, ")"};
                }
                if (c == '=' && i + 1 < s.size() && s[i + 1] == '=')
                {
                    i += 2;
                    return Token {Token::eEqEq, "=="};
                }
                if (c == '!' && i + 1 < s.size() && s[i + 1] == '=')
                {
                    i += 2;
                    return Token {Token::eNotEq, "!="};
                }
                if (c == '&' && i + 1 < s.size() && s[i + 1] == '&')
                {
                    i += 2;
                    return Token {Token::eAndAnd, "&&"};
                }
                if (c == '|' && i + 1 < s.size() && s[i + 1] == '|')
                {
                    i += 2;
                    return Token {Token::eOrOr, "||"};
                }

                // number
                if (std::isdigit(static_cast<unsigned char>(c)) != 0)
                {
                    size_t start = i;
                    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) != 0))
                        ++i;
                    return Token {Token::eNumber, std::string(s.substr(start, i - start))};
                }

                // ident
                if (isIdentStart(c))
                {
                    size_t start = i;
                    ++i;
                    while (i < s.size() && isIdentChar(s[i]))
                        ++i;
                    return Token {Token::eIdent, std::string(s.substr(start, i - start))};
                }

                // unknown char
                ++i;
                return Token {Token::eEnd, {}};
            }
        };

        struct Parser
        {
            Lexer                      lex;
            Token                      cur;
            const KeywordValueContext& ctx;

            explicit Parser(std::string_view text, const KeywordValueContext& c) : lex {Lexer {text, 0}}, ctx(c)
            {
                cur = lex.next();
            }

            void consume(Token::Kind k)
            {
                (void)k;
                cur = lex.next();
            }

            Result<uint32_t> resolveIdent(const std::string& name)
            {
                // true/false
                if (name == "true" || name == "TRUE" || name == "True")
                    return Result<uint32_t>::ok(1);
                if (name == "false" || name == "FALSE" || name == "False")
                    return Result<uint32_t>::ok(0);

                // keyword value
                auto it = ctx.values.find(name);
                if (it != ctx.values.end())
                    return Result<uint32_t>::ok(it->second);

                // enumerant lookup: try against all enum keyword decls (small set)
                // This supports constraints like SURFACE==CUTOUT.
                for (const auto& [kname, decl] : ctx.decls)
                {
                    if (!decl)
                        continue;
                    if (decl->kind != KeywordValueKind::eEnum)
                        continue;
                    for (uint32_t i = 0; i < static_cast<uint32_t>(decl->enumValues.size()); ++i)
                    {
                        if (decl->enumValues[i] == name)
                            return Result<uint32_t>::ok(i);
                    }
                }

                return Result<uint32_t>::err({ErrorCode::eParseError, "Unknown identifier in only_if: " + name});
            }

            Result<uint32_t> parsePrimaryValue()
            {
                if (cur.kind == Token::eIdent)
                {
                    auto name = cur.text;
                    consume(Token::eIdent);
                    return resolveIdent(name);
                }

                if (cur.kind == Token::eNumber)
                {
                    uint32_t v = 0;
                    for (char c : cur.text)
                        v = v * 10u + static_cast<uint32_t>(c - '0');
                    consume(Token::eNumber);
                    return Result<uint32_t>::ok(v);
                }

                if (cur.kind == Token::eLParen)
                {
                    consume(Token::eLParen);
                    auto r = parseExprBool();
                    if (!r.isOk())
                        return Result<uint32_t>::err(r.error());
                    if (cur.kind != Token::eRParen)
                        return Result<uint32_t>::err({ErrorCode::eParseError, "Expected ')' in only_if"});
                    consume(Token::eRParen);
                    return Result<uint32_t>::ok(r.value() ? 1u : 0u);
                }

                return Result<uint32_t>::err({ErrorCode::eParseError, "Expected primary in only_if"});
            }

            Result<bool> parseCmp()
            {
                // cmp := primary ( ('==' | '!=') primary )?
                auto lhs = parsePrimaryValue();
                if (!lhs.isOk())
                    return Result<bool>::err(lhs.error());

                if (cur.kind == Token::eEqEq || cur.kind == Token::eNotEq)
                {
                    const bool isEq = (cur.kind == Token::eEqEq);
                    consume(cur.kind);
                    auto rhs = parsePrimaryValue();
                    if (!rhs.isOk())
                        return Result<bool>::err(rhs.error());
                    const bool res = isEq ? (lhs.value() == rhs.value()) : (lhs.value() != rhs.value());
                    return Result<bool>::ok(res);
                }

                // no comparator: treat value as boolean
                return Result<bool>::ok(lhs.value() != 0);
            }

            Result<bool> parseAnd()
            {
                auto r = parseCmp();
                if (!r.isOk())
                    return r;
                bool v = r.value();
                while (cur.kind == Token::eAndAnd)
                {
                    consume(Token::eAndAnd);
                    auto rhs = parseCmp();
                    if (!rhs.isOk())
                        return rhs;
                    v = v && rhs.value();
                }
                return Result<bool>::ok(v);
            }

            Result<bool> parseOr()
            {
                auto r = parseAnd();
                if (!r.isOk())
                    return r;
                bool v = r.value();
                while (cur.kind == Token::eOrOr)
                {
                    consume(Token::eOrOr);
                    auto rhs = parseAnd();
                    if (!rhs.isOk())
                        return rhs;
                    v = v || rhs.value();
                }
                return Result<bool>::ok(v);
            }

            Result<bool> parseExprBool() { return parseOr(); }
        };

        inline std::string_view stripOnlyIf(std::string_view s)
        {
            // Accept "only_if(expr)" or raw "expr".
            auto trim = [](std::string_view v) {
                size_t a = 0;
                while (a < v.size() && std::isspace(static_cast<unsigned char>(v[a])) != 0)
                    ++a;
                size_t b = v.size();
                while (b > a && std::isspace(static_cast<unsigned char>(v[b - 1])) != 0)
                    --b;
                return v.substr(a, b - a);
            };

            s                            = trim(s);
            constexpr std::string_view k = "only_if";
            if (s.size() >= k.size() && s.substr(0, k.size()) == k)
            {
                auto lp = s.find('(');
                auto rp = s.rfind(')');
                if (lp != std::string_view::npos && rp != std::string_view::npos && rp > lp)
                    return trim(s.substr(lp + 1, rp - lp - 1));
            }
            return s;
        }
    } // namespace

    Result<bool> eval_only_if(std::string_view constraint, const KeywordValueContext& ctx)
    {
        auto expr = stripOnlyIf(constraint);
        if (expr.empty())
            return Result<bool>::ok(true);

        Parser p(expr, ctx);
        auto   r = p.parseExprBool();
        if (!r.isOk())
            return r;

        // Ensure full consumption
        if (p.cur.kind != Token::eEnd)
            return Result<bool>::err({ErrorCode::eParseError, "Trailing tokens in only_if expression"});

        return r;
    }
} // namespace vshadersystem
