#pragma once

// vshaderc-lib: the host/offline Slang compile core for vshadersystem v1.0.
//
// This library links the prebuilt Slang compiler and drives it in-process. It is
// DESKTOP-ONLY and is NOT part of the runtime `vshadersystem` library: engines link
// the runtime loader, never this. The compiler turns a single .slang module into
// per-entry-point SPIR-V (and, optionally, WGSL emitted directly by Slang), resolving
// `import`/`__include` against an in-memory VFS so shader libraries can be packaged and
// reused.
//
// Reflection of resources/material/keyword attributes (M2/M3) builds on top of this
// core and is added separately.

#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace vshaderc
{
    using vshadersystem::Result;
    using vshadersystem::ShaderStage;

    // An in-memory Slang source file exposed to the compiler's VFS. `path` is the
    // logical path used to resolve `import`/`#include` (forward slashes, e.g.
    // "vsh.slang" or "common/pbr.slang"); `import foo` resolves to "foo.slang".
    struct SlangSourceFile
    {
        std::string path;
        std::string text;
    };

    struct SlangDefine
    {
        std::string name;
        std::string value; // empty => defined as "1"
    };

    // Matrix memory layout for matrix-typed constants/uniforms. Slang's own default is
    // ROW-major; this library defaults to COLUMN-major to match column-major math libraries
    // (glm) and the GLSL/SPIR-V/Vulkan convention shared across the engine ecosystem. A shader
    // that mismatches this reads its matrices transposed (e.g. an MVP that collapses every
    // vertex). Per-field `row_major`/`column_major` qualifiers in the shader still override it.
    enum class MatrixLayout
    {
        Column, // column-major (default; matches glm)
        Row,    // row-major (Slang/HLSL native)
    };

    struct SlangCompileOptions
    {
        // In-memory files served to `import`/`#include` (the packaged shader library
        // plus the builtin vsh attribute module).
        std::vector<SlangSourceFile> vfsFiles;

        // Real-disk search directories (fallback for on-disk .slang sources/imports).
        std::vector<std::string> searchDirs;

        // Preprocessor macros (keyword variant defines).
        std::vector<SlangDefine> defines;

        // Which targets to emit. SPIR-V is the default; the others are opt-in. DXBC needs fxc
        // and DXIL needs dxc (both Windows-host at cook time); when a host cannot produce a
        // requested target, that blob is left empty and a note is logged (no hard failure).
        bool emitSpirv = true;
        bool emitWgsl  = false;
        bool emitDxbc  = false; // Direct3D 12, Shader Model 5.1
        bool emitDxil  = false; // Direct3D 12, Shader Model 6.0+

        // Target profiles (Slang profile names).
        std::string spirvProfile = "spirv_1_5";
        std::string dxbcProfile  = "sm_5_1";
        std::string dxilProfile  = "sm_6_0";

        // Memory layout for matrix constants (default column-major; see MatrixLayout).
        MatrixLayout matrixLayout = MatrixLayout::Column;

        bool optimize  = false;
        bool debugInfo = false;
    };

    struct SlangEntryPoint
    {
        std::string           name;
        ShaderStage           stage = ShaderStage::eUnknown;
        std::vector<uint32_t> spirv; // populated when emitSpirv
        std::string           wgsl;  // populated when emitWgsl
        std::vector<uint8_t>  dxbc;  // populated when emitDxbc (empty if the host lacks fxc)
        std::vector<uint8_t>  dxil;  // populated when emitDxil (empty if the host lacks dxc)
    };

    struct SlangCompileResult
    {
        std::vector<SlangEntryPoint> entryPoints;
        std::vector<std::string>     dependencies; // VFS/disk files the compile touched
        std::string                  log;          // diagnostics (warnings, etc.)
    };

    // Drives the Slang compiler in-process. Create once and reuse across compiles
    // (the global session and its core module are expensive to build).
    class SlangCompiler
    {
    public:
        SlangCompiler();
        ~SlangCompiler();

        SlangCompiler(const SlangCompiler&)            = delete;
        SlangCompiler& operator=(const SlangCompiler&) = delete;

        bool isValid() const;

        // Compile a single .slang module given by its source. `moduleName` is the
        // logical module name (no extension); `modulePath` is its logical path (used
        // for diagnostics and relative imports). All `[shader(...)]` entry points in
        // the module are compiled.
        Result<SlangCompileResult> compileModule(const std::string&        moduleName,
                                                 const std::string&        modulePath,
                                                 const std::string&        moduleSource,
                                                 const SlangCompileOptions& opt);

        // Returns the underlying slang::IGlobalSession* (as void*) so sibling
        // translation units (e.g. metadata extraction) can reuse it without a header
        // dependency on the Slang headers. Null if construction failed.
        void* nativeGlobalSession() const;

    private:
        struct Impl;
        Impl* m_Impl = nullptr;
    };

    // Map a Slang stage (SlangStage) to our ShaderStage. Exposed for reflection code.
    ShaderStage shader_stage_from_slang(int slangStage);
} // namespace vshaderc
