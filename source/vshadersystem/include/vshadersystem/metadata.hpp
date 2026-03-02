#pragma once

#include "vshadersystem/keywords.hpp"
#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace vshadersystem
{
    struct ParsedMetadata
    {
        bool           hasMaterialDecl    = false;
        std::string    materialStructName = "Material";
        ShaderLanguage language           = ShaderLanguage::eAuto;
        uint32_t       renderQueue        = 2000;

        // Entry points for single-file multi-stage shaders.
        // Defaults to "main" for all stages.
        std::string entryVert = "main";
        std::string entryFrag = "main";
        std::string entryComp = "main";
        std::string entryTask = "main";
        std::string entryMesh = "main";

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
