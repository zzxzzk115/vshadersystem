#pragma once

#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"
#include "vshadersystem/keywords.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace vshadersystem
{
    struct ParsedMetadata
    {
        bool hasMaterialDecl = false;

        // name -> semantic/default/range
        struct ParamMeta
        {
            Semantic     semantic   = Semantic::eUnknown;
            bool         hasDefault = false;
            ParamDefault defaultValue {};
            bool         hasRange = false;
            ParamRange   range {};
        };

        struct TextureMeta
        {
            Semantic semantic = Semantic::eUnknown;
        };

        std::unordered_map<std::string, ParamMeta>   params;
        std::unordered_map<std::string, TextureMeta> textures;

        // Keyword declarations parsed from #pragma keyword ... lines
        std::vector<KeywordDecl> keywords;

        RenderState renderState {};
        bool        renderStateExplicit = false;
    };

    // Parse `#pragma vultra ...` lines. We keep grammar intentionally small and strict.
    Result<ParsedMetadata> parse_vultra_metadata(std::string_view sourceText);
} // namespace vshadersystem
