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

        // Optional engine-wide keyword values (typically global scope), used for
        // resolving permutation keyword values and computing ShaderBinary::variantHash.
        bool              hasEngineKeywords = false;
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

    Result<BuildResult> build_shader(const BuildRequest& req);

    // Utility: build from SPIR-V input and still generate reflection + material description.
    Result<ShaderBinary> build_from_spirv(const std::vector<uint32_t>& spirv, ShaderStage stage);
} // namespace vshadersystem
