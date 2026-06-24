#pragma once

// Metadata extracted from a .slang module's vsh user-defined attributes via the Slang
// Reflection API. This is the authoring-intent layer (declarations); the concrete
// material byte layout / descriptor bindings come from program-layout reflection (M3)
// and are merged with this.

#include "vshaderc/slang_compiler.hpp"

#include "vshadersystem/keywords.hpp"
#include "vshadersystem/types.hpp"

#include <string>
#include <vector>

namespace vshaderc
{
    // A material struct field annotated with vsh attributes.
    struct MaterialFieldMeta
    {
        std::string name;          // field name in the [VshMaterial] struct
        std::string semantic;      // from [VshSemantic("...")], empty if none
        bool        hasRange = false;
        float       rangeLo  = 0.0f;
        float       rangeHi  = 0.0f;
        std::string textureKind;   // from [VshTexture("Texture2D")], empty if not a texture

        // Editor hints.
        bool        hasDefault = false;
        std::string defaultValue;  // from [VshDefault("...")], comma-separated numbers
        bool        isColor = false;     // from [VshColor]
        std::string displayName;         // from [VshDisplayName("...")]
    };

    struct ShaderMetadata
    {
        // [VshMaterial] struct (empty if the shader declares no material).
        std::string                    materialStructName;
        std::vector<MaterialFieldMeta> materialFields;

        // [VshKeyword(...)] declarations (typically on a __vsh_meta marker function).
        std::vector<vshadersystem::KeywordDecl> keywords;

        // [VshRenderState(...)] -> merged render state. hasRenderState is true if any
        // VshRenderState attribute was present.
        bool                       hasRenderState = false;
        vshadersystem::RenderState renderState;
        std::vector<std::pair<std::string, std::string>> renderStateRaw; // key,value (diagnostics)
    };

    // Load a .slang module and read its vsh attributes via the Slang Reflection API.
    // Reuses `compiler`'s global session. Does not require codegen/link.
    Result<ShaderMetadata> extract_shader_metadata(const SlangCompiler&       compiler,
                                                   const std::string&         moduleName,
                                                   const std::string&         modulePath,
                                                   const std::string&         moduleSource,
                                                   const SlangCompileOptions& opt);
} // namespace vshaderc
