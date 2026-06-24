#pragma once

// Internal helpers shared by the compile and metadata-extraction paths.

#include "vshaderc/slang_compiler.hpp"

#include "slang_vfs.hpp"

#include <string>

namespace vshaderc::detail
{
    // Logical path of the builtin vsh attribute module (auto-mounted into every compile).
    inline constexpr const char* kVshModulePath = "vsh.slang";

    // Source of the builtin vsh attribute module.
    const char* vsh_module_source();

    // Populate `fs` with: the builtin vsh module, the caller's VFS files, the top module
    // source itself, and the configured search directories.
    void populate_filesystem(MemoryFileSystem&          fs,
                             const SlangCompileOptions& opt,
                             const std::string&         modulePath,
                             const std::string&         moduleSource);
} // namespace vshaderc::detail
