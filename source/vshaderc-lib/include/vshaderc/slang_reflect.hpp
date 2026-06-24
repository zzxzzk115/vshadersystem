#pragma once

// Maps Slang's program layout into vshadersystem's reflection + material structures.
// Convention (matching VRI): resources carry explicit `[[vk::binding(binding, set)]]`,
// read back via Slang's VariableLayoutReflection binding index/space. The vsh attribute
// metadata (semantics/ranges/textures/render state) from extract_shader_metadata() is
// merged in to produce the MaterialDescription.

#include "vshaderc/slang_compiler.hpp"
#include "vshaderc/slang_metadata.hpp"

#include "vshadersystem/types.hpp"

#include <string>

namespace vshaderc
{
    struct ProgramReflection
    {
        vshadersystem::ShaderReflection    reflection; // descriptors + blocks + local size
        vshadersystem::MaterialDescription material;   // merged with vsh metadata
    };

    // Load + link the module and reflect its global parameters into ShaderReflection,
    // then build MaterialDescription by merging the block layout with `meta`.
    Result<ProgramReflection> reflect_shader(const SlangCompiler&       compiler,
                                             const std::string&         moduleName,
                                             const std::string&         modulePath,
                                             const std::string&         moduleSource,
                                             const SlangCompileOptions& opt,
                                             const ShaderMetadata&      meta);
} // namespace vshaderc
