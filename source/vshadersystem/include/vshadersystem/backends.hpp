#pragma once

#include "vshadersystem/compiler.hpp"
#include "vshadersystem/reflect.hpp"
#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <memory>
#include <string>

namespace vshadersystem
{
    // ============================================================================
    // Backend interfaces
    //
    // vshadersystem is a tool-layer compiler. These backends provide language-
    // specific compilation, reflection and optional source injection.
    // ============================================================================

    class ICompilerBackend
    {
    public:
        virtual ~ICompilerBackend()                                                                = default;
        virtual Result<CompileOutput> compile(const SourceInput& input, const CompileOptions& opt) = 0;
    };

    class IReflectorBackend
    {
    public:
        virtual ~IReflectorBackend() = default;
        virtual Result<ShaderReflection>
        reflect(const std::vector<uint32_t>& spirv, const ReflectionOptions& opt, const CompileOutput* compileOut) = 0;
    };

    class IMaterialInjector
    {
    public:
        virtual ~IMaterialInjector() = default;

        // Returns injectedText + originalSource (or originalSource unchanged).
        virtual std::string inject(const std::string& originalSource,
                                   const std::string& materialStructName,
                                   MaterialAccessMode mode,
                                   const std::string& extraAfterMaterialAccess) = 0;
    };

    struct BackendBundle
    {
        std::unique_ptr<ICompilerBackend>  compiler;
        std::unique_ptr<IReflectorBackend> reflector;
        std::unique_ptr<IMaterialInjector> injector;
    };

    // Factory: select backend set for the requested language.
    BackendBundle create_backends(ShaderLanguage lang);

} // namespace vshadersystem
