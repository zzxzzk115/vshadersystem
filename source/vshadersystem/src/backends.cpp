#include "vshadersystem/backends.hpp"
#include "vshadersystem/compiler.hpp"
#include "vshadersystem/reflect.hpp"

#include <memory>
#include <string>

namespace vshadersystem
{

    // ============================================================
    // Compiler backends
    // ============================================================

    class GLSLCompilerBackend final : public ICompilerBackend
    {
    public:
        Result<CompileOutput> compile(const SourceInput& input, const CompileOptions& opt) override
        {
            return compile_glsl_to_spirv(input, opt);
        }
    };

    // ============================================================
    // Reflectors
    // ============================================================

    class SpirvCrossReflectorBackend final : public IReflectorBackend
    {
    public:
        Result<ShaderReflection>
        reflect(const std::vector<uint32_t>& spirv, const ReflectionOptions& opt, const CompileOutput*) override
        {
            return reflect_spirv(spirv, opt);
        }
    };

    static size_t find_struct_end(const std::string& src, const std::string& structName)
    {
        const std::string key = "struct " + structName;

        const size_t pos = src.find(key);

        if (pos == std::string::npos)
            return std::string::npos;

        const size_t braceOpen = src.find('{', pos);

        if (braceOpen == std::string::npos)
            return std::string::npos;

        int depth = 1;

        size_t i = braceOpen + 1;

        for (; i < src.size(); ++i)
        {
            const char c = src[i];

            if (c == '{')
            {
                depth++;
            }
            else if (c == '}')
            {
                depth--;

                if (depth == 0)
                {
                    i++;

                    if (i < src.size() && src[i] == ';')
                        i++;

                    return i;
                }
            }
        }

        return std::string::npos;
    }

    static std::string
    inject_after_struct(const std::string& src, const std::string& structName, const std::string& injectCode)
    {

        if (structName.empty())
            return injectCode + src;

        const size_t pos = find_struct_end(src, structName);

        if (pos == std::string::npos)
            return injectCode + src;

        std::string out;

        out.reserve(src.size() + injectCode.size());

        out.append(src.data(), pos);

        out.append("\n");
        out.append(injectCode);
        out.append("\n");

        out.append(src.data() + pos, src.size() - pos);

        return out;
    }

    // ============================================================
    // Injector classes
    // ============================================================

    class GLSLMaterialInjector final : public IMaterialInjector
    {
    public:
        std::string inject(const std::string& src,
                           const std::string& structName,
                           MaterialAccessMode mode,
                           const std::string& extraAfterMaterialAccess) override
        {
            (void)mode;
            // v0.5+ (injection-only): resource layouts / material load functions must be provided
            // by the caller (CompileOptions::materialInjection.preamble or other includes).
            // This injector only splices the helper macros/functions after the Material struct.
            return inject_after_struct(src, structName, extraAfterMaterialAccess);
        }
    };

    BackendBundle create_backends(ShaderLanguage lang)
    {

        BackendBundle out;

        switch (lang)
        {
            case ShaderLanguage::eGLSL:

            case ShaderLanguage::eAuto:

            default:

                out.compiler = std::make_unique<GLSLCompilerBackend>();

                out.reflector = std::make_unique<SpirvCrossReflectorBackend>();

                out.injector = std::make_unique<GLSLMaterialInjector>();

                break;
        }

        return out;
    }

} // namespace vshadersystem
