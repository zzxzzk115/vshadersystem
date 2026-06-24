#pragma once

// Backend portability validation: cross-compile a shader through Slang to each backend's
// shading language and report whether codegen succeeds. This is how we assert that an
// engine shader is usable across Vulkan / OpenGL / WebGPU / D3D12 / Metal without
// pulling in five native validators.

#include "vshaderc/slang_compiler.hpp"

#include <string>
#include <vector>

namespace vshaderc
{
    enum class Backend
    {
        Vulkan, // SPIR-V
        OpenGL, // GLSL
        WebGPU, // WGSL
        D3D12,  // HLSL
        Metal,  // Metal Shading Language
    };

    struct BackendResult
    {
        Backend     backend;
        bool        ok = false;
        size_t      outputSize = 0; // bytes of generated code (sanity: > 0 on success)
        std::string log;            // diagnostics on failure
    };

    const char* backend_name(Backend b);

    // Compile `src` to each requested backend target and report success per backend.
    // Reuses the builtin vsh module + the options' VFS so attribute-annotated shaders
    // validate too.
    std::vector<BackendResult> validate_backends(SlangCompiler&             compiler,
                                                 const std::string&         moduleName,
                                                 const std::string&         modulePath,
                                                 const std::string&         moduleSource,
                                                 const SlangCompileOptions& opt,
                                                 const std::vector<Backend>& backends);
} // namespace vshaderc
