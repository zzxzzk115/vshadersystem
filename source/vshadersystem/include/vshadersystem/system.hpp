#pragma once

#include "vshadersystem/compiler.hpp"
#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <string>

namespace vshadersystem
{
    struct BuildRequest
    {
        SourceInput    source;
        CompileOptions options;

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
