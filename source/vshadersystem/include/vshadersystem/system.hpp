#pragma once

#include "vshadersystem/compiler.hpp"
#include "vshadersystem/engine_keywords.hpp"
#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <string>

namespace vshadersystem
{
    struct BuildRequest
    {
        SourceInput    source;
        CompileOptions options;

        // Optional explicit shader id override. Used when the source has no
        // INI-style `[vshader] id = "..."` (e.g. raw GLSL provided directly).
        // Exactly one source of id is required: this field or the metadata id.
        std::string id;

        // Optional engine-wide keyword values (typically global scope), used for
        // resolving permutation keyword values and computing ShaderBinary::variantHash.
        bool               hasEngineKeywords = false;
        EngineKeywordsFile engineKeywords;

        // Cache behavior
        bool        enableCache = true;
        std::string cacheDir    = ".vshader_cache";
    };

    struct BuildResult
    {
        ShaderBinary binary;
        std::string  log;
        bool         fromCache = false;
    };

    Result<BuildResult> build_single_shader(const BuildRequest& req);

    Result<std::unordered_map<ShaderStage, BuildResult>> build_multiple_shaders(const BuildRequest& req);

    // Utility: build from SPIR-V input and still generate reflection + material description.
    Result<ShaderBinary> build_from_spirv(const std::vector<uint32_t>& spirv, ShaderStage stage);
} // namespace vshadersystem
