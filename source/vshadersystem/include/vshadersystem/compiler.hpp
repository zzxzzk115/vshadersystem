#pragma once

#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <string>
#include <vector>

namespace vshadersystem
{
    struct Define
    {
        std::string name;
        std::string value;
    };

    struct CompileOptions
    {
        ShaderStage stage = ShaderStage::eFrag;

        // Target SPIR-V version. glslang uses Vulkan/OpenGL envs; we expose a minimal target here.
        int spirvVersion = 0; // 0 = default for environment

        bool optimize       = false;
        bool debugInfo      = false;
        bool stripDebugInfo = false;

        std::vector<Define>      defines;
        std::vector<std::string> includeDirs;

        // We can extend this with macro stripping, warnings as errors, etc.
    };

    struct SourceInput
    {
        std::string virtualPath; // used for includes and diagnostics
        std::string sourceText;
    };

    struct CompileOutput
    {
        std::vector<uint32_t>    spirv;
        std::string              infoLog;
        std::vector<std::string> dependencies;
    };

    Result<CompileOutput> compile_glsl_to_spirv(const SourceInput& input, const CompileOptions& opt);
} // namespace vshadersystem
