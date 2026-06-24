#include "vshaderc/slang_validate.hpp"

#include "slang_internal.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

namespace vshaderc
{
    using Slang::ComPtr;

    const char* backend_name(Backend b)
    {
        switch (b)
        {
            case Backend::Vulkan: return "Vulkan (SPIR-V)";
            case Backend::OpenGL: return "OpenGL (GLSL)";
            case Backend::WebGPU: return "WebGPU (WGSL)";
            case Backend::D3D12: return "D3D12 (HLSL)";
            case Backend::Metal: return "Metal (MSL)";
        }
        return "?";
    }

    namespace
    {
        struct TargetSpec
        {
            SlangCompileTarget format;
            const char*        profile; // null if none
            bool               emitSpirvDirectly;
        };

        TargetSpec spec_for(Backend b)
        {
            switch (b)
            {
                case Backend::Vulkan: return {SLANG_SPIRV, "spirv_1_5", true};
                case Backend::OpenGL: return {SLANG_GLSL, "glsl_450", false};
                case Backend::WebGPU: return {SLANG_WGSL, nullptr, false};
                case Backend::D3D12: return {SLANG_HLSL, "sm_6_0", false};
                case Backend::Metal: return {SLANG_METAL, nullptr, false};
            }
            return {SLANG_SPIRV, "spirv_1_5", true};
        }

        std::string blob_str(slang::IBlob* b)
        {
            if (!b || b->getBufferSize() == 0)
                return {};
            return std::string(static_cast<const char*>(b->getBufferPointer()), b->getBufferSize());
        }

        BackendResult validate_one(slang::IGlobalSession*     global,
                                   Backend                    backend,
                                   const std::string&         moduleName,
                                   const std::string&         topPath,
                                   const std::string&         source,
                                   const SlangCompileOptions& opt)
        {
            BackendResult result;
            result.backend = backend;

            const TargetSpec ts = spec_for(backend);

            slang::TargetDesc target = {};
            target.format            = ts.format;
            if (ts.profile)
                target.profile = global->findProfile(ts.profile);

            std::vector<slang::CompilerOptionEntry> coptions;
            if (ts.emitSpirvDirectly)
            {
                slang::CompilerOptionEntry e = {};
                e.name                       = slang::CompilerOptionName::EmitSpirvDirectly;
                e.value.intValue0            = 1;
                coptions.push_back(e);
            }

            detail::MemoryFileSystem fs;
            detail::populate_filesystem(fs, opt, topPath, source);

            const char*        searchPaths[] = {""};
            slang::SessionDesc sd            = {};
            sd.targets                       = &target;
            sd.targetCount                   = 1;
            sd.searchPaths                   = searchPaths;
            sd.searchPathCount               = 1;
            sd.fileSystem                    = &fs;
            if (!coptions.empty())
            {
                sd.compilerOptionEntries    = coptions.data();
                sd.compilerOptionEntryCount = static_cast<uint32_t>(coptions.size());
            }

            ComPtr<slang::ISession> session;
            if (SLANG_FAILED(global->createSession(sd, session.writeRef())))
            {
                result.log = "createSession failed";
                return result;
            }

            ComPtr<slang::IBlob> diag;
            slang::IModule*      module =
                session->loadModuleFromSourceString(moduleName.c_str(), topPath.c_str(), source.c_str(),
                                                    diag.writeRef());
            if (!module)
            {
                result.log = blob_str(diag);
                return result;
            }

            std::vector<slang::IComponentType*>     comps{module};
            std::vector<ComPtr<slang::IEntryPoint>> eps;
            for (SlangInt32 i = 0, n = module->getDefinedEntryPointCount(); i < n; ++i)
            {
                ComPtr<slang::IEntryPoint> ep;
                if (SLANG_SUCCEEDED(module->getDefinedEntryPoint(i, ep.writeRef())) && ep)
                {
                    comps.push_back(ep.get());
                    eps.push_back(ep);
                }
            }

            ComPtr<slang::IComponentType> composed;
            diag = nullptr;
            session->createCompositeComponentType(comps.data(), static_cast<SlangInt>(comps.size()),
                                                  composed.writeRef(), diag.writeRef());
            if (!composed)
            {
                result.log = blob_str(diag);
                return result;
            }
            ComPtr<slang::IComponentType> linked;
            diag = nullptr;
            composed->link(linked.writeRef(), diag.writeRef());
            if (!linked)
            {
                result.log = blob_str(diag);
                return result;
            }

            // Generate code for every entry point; failure on any is a backend failure.
            size_t totalSize = 0;
            for (SlangInt32 i = 0; i < static_cast<SlangInt32>(eps.size()); ++i)
            {
                ComPtr<slang::IBlob> code;
                diag = nullptr;
                if (SLANG_FAILED(linked->getEntryPointCode(i, 0, code.writeRef(), diag.writeRef())) || !code ||
                    code->getBufferSize() == 0)
                {
                    result.log = blob_str(diag);
                    return result;
                }
                totalSize += code->getBufferSize();
            }

            result.ok         = true;
            result.outputSize = totalSize;
            return result;
        }
    } // namespace

    std::vector<BackendResult> validate_backends(SlangCompiler&              compiler,
                                                 const std::string&          moduleName,
                                                 const std::string&          modulePath,
                                                 const std::string&          moduleSource,
                                                 const SlangCompileOptions&  opt,
                                                 const std::vector<Backend>& backends)
    {
        std::vector<BackendResult> out;
        auto* global = static_cast<slang::IGlobalSession*>(compiler.nativeGlobalSession());
        if (!global)
            return out;
        const std::string topPath = modulePath.empty() ? (moduleName + ".slang") : modulePath;
        for (Backend b : backends)
            out.push_back(validate_one(global, b, moduleName, topPath, moduleSource, opt));
        return out;
    }
} // namespace vshaderc
