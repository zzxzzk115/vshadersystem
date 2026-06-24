#include "slang_internal.hpp"

namespace vshaderc::detail
{
    // The builtin vsh attribute library. Authors `import vsh;` and annotate their
    // material struct / fields / a marker function. The compiler reads these back via
    // the Slang Reflection API (see slang_metadata.cpp). Attribute argument encoding:
    //   VshKeyword(name, type, dispatch, scope)
    //     type:     "bool" | "enum:lab0,lab1,..."
    //     dispatch: "permute" | "runtime" | "special"
    //     scope:    "local" | "global" | "material" | "pass"
    //   VshRenderState(key, value)  e.g. ("cull","back"), ("depth_test","on")
    //   VshSemantic(name) / VshRange(lo,hi) / VshTexture(kind) on material fields.
    const char* vsh_module_source()
    {
        return R"VSH(
// vshadersystem builtin attribute library (v1.0)
module vsh;

[__AttributeUsage(_AttributeTargets.Struct)]
public struct VshMaterialAttribute {}

[__AttributeUsage(_AttributeTargets.Var)]
public struct VshSemanticAttribute { string name; }

[__AttributeUsage(_AttributeTargets.Var)]
public struct VshRangeAttribute { float lo; float hi; }

[__AttributeUsage(_AttributeTargets.Var)]
public struct VshTextureAttribute { string kind; }

[__AttributeUsage(_AttributeTargets.Function)]
public struct VshKeywordAttribute { string name; string type; string dispatch; string scope; }

[__AttributeUsage(_AttributeTargets.Function)]
public struct VshRenderStateAttribute { string key; string value; }
)VSH";
    }

    void populate_filesystem(MemoryFileSystem&          fs,
                             const SlangCompileOptions& opt,
                             const std::string&         modulePath,
                             const std::string&         moduleSource)
    {
        // Builtin vsh module is always available to `import vsh`.
        fs.addFile(kVshModulePath, vsh_module_source());

        for (const auto& f : opt.vfsFiles)
            fs.addFile(f.path, f.text);

        // Expose the top module under its logical path so sibling imports resolve.
        if (!modulePath.empty())
            fs.addFile(modulePath, moduleSource);

        for (const auto& dir : opt.searchDirs)
            fs.addSearchDir(dir);
    }
} // namespace vshaderc::detail
