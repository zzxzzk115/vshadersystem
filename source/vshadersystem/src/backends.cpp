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

    class SlangCompilerBackend final : public ICompilerBackend
    {
    public:
        Result<CompileOutput> compile(const SourceInput& input, const CompileOptions& opt) override
        {
            return compile_slang_to_spirv(input, opt);
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

    class SlangReflectorBackend final : public IReflectorBackend
    {
    public:
        Result<ShaderReflection>
        reflect(const std::vector<uint32_t>& spirv, const ReflectionOptions& opt, const CompileOutput*) override
        {
            // unified path
            return reflect_spirv(spirv, opt);
        }
    };

    // ============================================================
    // Injection helpers
    // ============================================================

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
    // GLSL material injection
    // ============================================================

    static std::string inject_glsl_material_access(const std::string& materialStructName, MaterialAccessMode mode)
    {

        switch (mode)
        {

            case MaterialAccessMode::eBDA:

                return "#extension GL_EXT_buffer_reference2 : require\n"
                       "#extension GL_EXT_scalar_block_layout : require\n"
                       "#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require\n"
                       "\n"
                       "layout(buffer_reference, scalar) readonly buffer vshader_MaterialRef\n"
                       "{\n"
                       "    " +
                       materialStructName +
                       " material;\n"
                       "};\n"
                       "\n" +
                       materialStructName +
                       " vshader_LoadMaterial(uint64_t addr)\n"
                       "{\n"
                       "    return vshader_MaterialRef(addr).material;\n"
                       "}\n";

            case MaterialAccessMode::eUBO:

                return "layout(set = 0, binding = 0, std140) uniform vshader_MaterialUBO\n"
                       "{\n"
                       "    " +
                       materialStructName +
                       " material;\n"
                       "} vshader_Material;\n"
                       "\n" +
                       materialStructName +
                       " vshader_LoadMaterial()\n"
                       "{\n"
                       "    return vshader_Material.material;\n"
                       "}\n";

            case MaterialAccessMode::eSSBO:

                return "layout(set = 0, binding = 0, std430) readonly buffer vshader_MaterialSSBO\n"
                       "{\n"
                       "    " +
                       materialStructName +
                       " materials[];\n"
                       "} vshader_Materials;\n"
                       "\n" +
                       materialStructName +
                       " vshader_LoadMaterial(uint index)\n"
                       "{\n"
                       "    return vshader_Materials.materials[index];\n"
                       "}\n";

            case MaterialAccessMode::ePushConstant:

                return "layout(push_constant) uniform vshader_MaterialPC\n"
                       "{\n"
                       "    " +
                       materialStructName +
                       " material;\n"
                       "} vshader_Material;\n"
                       "\n" +
                       materialStructName +
                       " vshader_LoadMaterial()\n"
                       "{\n"
                       "    return vshader_Material.material;\n"
                       "}\n";

            default:

                return {};
        }
    }

    // ============================================================
    // Slang material injection
    // ============================================================

    static std::string inject_slang_material_access(const std::string& materialStructName, MaterialAccessMode mode)
    {

        switch (mode)
        {

            case MaterialAccessMode::eBDA:

                return "[[vk::buffer_reference, vk::buffer_reference_align(16)]]\n"
                       "struct vshader_MaterialRef\n"
                       "{\n"
                       "    " +
                       materialStructName +
                       " material;\n"
                       "};\n"
                       "\n" +
                       materialStructName +
                       " vshader_LoadMaterial(uint64_t addr)\n"
                       "{\n"
                       "    return ((vshader_MaterialRef*)addr)->material;\n"
                       "}\n";

            case MaterialAccessMode::eUBO:

                return "cbuffer vshader_MaterialUBO : register(b0)\n"
                       "{\n"
                       "    " +
                       materialStructName +
                       " vshader_material;\n"
                       "};\n"
                       "\n" +
                       materialStructName +
                       " vshader_LoadMaterial()\n"
                       "{\n"
                       "    return vshader_material;\n"
                       "}\n";

            case MaterialAccessMode::eSSBO:

                return "StructuredBuffer<" + materialStructName +
                       "> vshader_materials : register(t0);\n"
                       "\n" +
                       materialStructName +
                       " vshader_LoadMaterial(uint index)\n"
                       "{\n"
                       "    return vshader_materials[index];\n"
                       "}\n";

            case MaterialAccessMode::ePushConstant:

                return "[[vk::push_constant]]\n"
                       "struct vshader_MaterialPC\n"
                       "{\n"
                       "    " +
                       materialStructName +
                       " material;\n"
                       "};\n"
                       "\n"
                       "vshader_MaterialPC vshader_pc;\n"
                       "\n" +
                       materialStructName +
                       " vshader_LoadMaterial()\n"
                       "{\n"
                       "    return vshader_pc.material;\n"
                       "}\n";

            default:

                return {};
        }
    }

    // ============================================================
    // Injector classes
    // ============================================================

    class GLSLMaterialInjector final : public IMaterialInjector
    {
    public:
        std::string inject(const std::string& src, const std::string& structName, MaterialAccessMode mode) override
        {

            const std::string code = inject_glsl_material_access(structName, mode);

            return inject_after_struct(src, structName, code);
        }
    };

    class SlangMaterialInjector final : public IMaterialInjector
    {
    public:
        std::string inject(const std::string& src, const std::string& structName, MaterialAccessMode mode) override
        {

            const std::string code = inject_slang_material_access(structName, mode);

            return inject_after_struct(src, structName, code);
        }
    };

    // ============================================================
    // Factory
    // ============================================================

    BackendBundle create_backends(ShaderLanguage lang)
    {

        BackendBundle out;

        switch (lang)
        {

            case ShaderLanguage::eSlang:

                out.compiler = std::make_unique<SlangCompilerBackend>();

                out.reflector = std::make_unique<SlangReflectorBackend>();

                out.injector = std::make_unique<SlangMaterialInjector>();

                break;

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