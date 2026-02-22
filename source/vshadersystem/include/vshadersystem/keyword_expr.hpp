#pragma once

#include "vshadersystem/keywords.hpp"
#include "vshadersystem/result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace vshadersystem
{
    // Evaluates a small boolean expression used by keyword constraints.
    // Supported grammar (intentionally small):
    //
    //   expr  := or
    //   or    := and ( '||' and )*
    //   and   := cmp ( '&&' cmp )*
    //   cmp   := primary ( ('==' | '!=') primary )?
    //   primary := IDENT | NUMBER | 'true' | 'false' | '(' expr ')'
    //
    // IDENT resolution:
    //   - if IDENT matches a keyword name, it resolves to that keyword's numeric value
    //   - otherwise, if the current keyword is enum, IDENT can be an enumerant and resolves to its index
    //   - otherwise, error
    //
    // NUMBER is parsed as uint32.

    struct KeywordValueContext
    {
        // keyword name -> numeric value (bool: 0/1, enum: index)
        std::unordered_map<std::string, uint32_t> values;

        // keyword name -> decl (for enum enumerant lookup)
        std::unordered_map<std::string, const KeywordDecl*> decls;
    };

    // Parse and evaluate an only_if(...) constraint.
    // The input may be either "only_if(<expr>)" or just "<expr>".
    Result<bool> eval_only_if(std::string_view constraint, const KeywordValueContext& ctx);

} // namespace vshadersystem
