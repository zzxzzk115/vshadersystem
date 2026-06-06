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
        // v0.5.0: structured INI-style shader header.
        // When true, metadata was parsed from [vshader]/[properties]/[keywords]/[renderstate] blocks.
        bool isIniStyle = false;
        // Optional code injected into the shared section (before any stage markers).
        // Used by ini-style shaders to auto-generate the Material struct.
        std::string generatedPreamble;

        // Explicit, stable logical shader id (from `[vshader] id = "..."`).
        // Required for INI-style shaders: the build uses this as the shader's
        // identity for variant lookup instead of deriving it from the file stem.
        std::string id;

        bool           hasMaterialDecl    = false;
        std::string    materialStructName = "Material";
        ShaderLanguage language           = ShaderLanguage::eAuto;

        // GLSL: version number for the generated #version directive.
        // Only used for INI-style shaders (v0.5+) where #version is typically not authored per-stage.
        // Default is 460.
        uint32_t glslVersion = 460;
        uint32_t renderQueue = 2000;

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
            bool      hasType = false;
            ParamType type    = ParamType::eFloat;

            Semantic     semantic   = Semantic::eUnknown;
            bool         hasDefault = false;
            ParamDefault defaultValue {};
            bool         hasRange = false;
            ParamRange   range {};

            std::vector<MaterialParamDesc::EnumOption> enumOptions;
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
