#include "vshaderc/slang_compiler.hpp"

#include "slang_vfs.hpp"

#include <slang-com-ptr.h>
#include <slang.h>

namespace vshaderc
{
    using Slang::ComPtr;
    using vshadersystem::Error;
    using vshadersystem::ErrorCode;

    ShaderStage shader_stage_from_slang(int slangStage)
    {
        switch (slangStage)
        {
            case SLANG_STAGE_VERTEX: return ShaderStage::eVert;
            case SLANG_STAGE_FRAGMENT: return ShaderStage::eFrag;
            case SLANG_STAGE_GEOMETRY: return ShaderStage::eGeom;
            case SLANG_STAGE_COMPUTE: return ShaderStage::eComp;
            case SLANG_STAGE_AMPLIFICATION: return ShaderStage::eTask;
            case SLANG_STAGE_MESH: return ShaderStage::eMesh;
            case SLANG_STAGE_RAY_GENERATION: return ShaderStage::eRgen;
            case SLANG_STAGE_MISS: return ShaderStage::eRmiss;
            case SLANG_STAGE_CLOSEST_HIT: return ShaderStage::eRchit;
            case SLANG_STAGE_ANY_HIT: return ShaderStage::eRahit;
            case SLANG_STAGE_INTERSECTION: return ShaderStage::eRint;
            default: return ShaderStage::eUnknown;
        }
    }

    struct SlangCompiler::Impl
    {
        ComPtr<slang::IGlobalSession> global;
    };

    SlangCompiler::SlangCompiler()
    {
        m_Impl = new Impl();
        slang::createGlobalSession(m_Impl->global.writeRef());
    }

    SlangCompiler::~SlangCompiler() { delete m_Impl; }

    bool SlangCompiler::isValid() const { return m_Impl && m_Impl->global != nullptr; }

    static std::string blob_to_string(slang::IBlob* b)
    {
        if (!b || b->getBufferSize() == 0)
            return {};
        return std::string(static_cast<const char*>(b->getBufferPointer()), b->getBufferSize());
    }

    Result<SlangCompileResult> SlangCompiler::compileModule(const std::string&        moduleName,
                                                            const std::string&        modulePath,
                                                            const std::string&        moduleSource,
                                                            const SlangCompileOptions& opt)
    {
        using R = Result<SlangCompileResult>;
        if (!isValid())
            return R::err({ErrorCode::eCompileError, "Slang global session unavailable"});

        slang::IGlobalSession* global = m_Impl->global;

        // --- targets: SPIR-V (always) + optional WGSL ---
        std::vector<slang::TargetDesc> targets;
        int spirvIndex = -1;
        int wgslIndex  = -1;
        {
            slang::TargetDesc spv = {};
            spv.format            = SLANG_SPIRV;
            spv.profile           = global->findProfile(opt.spirvProfile.c_str());
            spirvIndex            = static_cast<int>(targets.size());
            targets.push_back(spv);
        }
        if (opt.emitWgsl)
        {
            slang::TargetDesc wg = {};
            wg.format            = SLANG_WGSL;
            wgslIndex            = static_cast<int>(targets.size());
            targets.push_back(wg);
        }

        // --- compiler options ---
        std::vector<slang::CompilerOptionEntry> coptions;
        {
            slang::CompilerOptionEntry e = {};
            e.name                       = slang::CompilerOptionName::EmitSpirvDirectly;
            e.value.intValue0            = 1;
            coptions.push_back(e);
        }
        {
            slang::CompilerOptionEntry e = {};
            e.name                       = slang::CompilerOptionName::VulkanUseEntryPointName;
            e.value.intValue0            = 1;
            coptions.push_back(e);
        }
        if (opt.debugInfo)
        {
            slang::CompilerOptionEntry e = {};
            e.name                       = slang::CompilerOptionName::DebugInformation;
            e.value.intValue0            = SLANG_DEBUG_INFO_LEVEL_STANDARD;
            coptions.push_back(e);
        }

        // --- preprocessor macros (keyword variant defines) ---
        std::vector<slang::PreprocessorMacroDesc> macros;
        macros.reserve(opt.defines.size());
        for (const auto& d : opt.defines)
            macros.push_back({d.name.c_str(), d.value.empty() ? "1" : d.value.c_str()});

        // --- VFS file system ---
        detail::MemoryFileSystem fs;
        for (const auto& f : opt.vfsFiles)
            fs.addFile(f.path, f.text);
        // Expose the top module under its logical path too (so sibling imports resolve).
        fs.addFile(modulePath.empty() ? (moduleName + ".slang") : modulePath, moduleSource);
        for (const auto& dir : opt.searchDirs)
            fs.addSearchDir(dir);

        // searchPaths: root, so `import foo` resolves to "foo.slang" via the file system.
        const char* searchPaths[] = {""};

        slang::SessionDesc sessionDesc = {};
        sessionDesc.targets            = targets.data();
        sessionDesc.targetCount        = static_cast<SlangInt>(targets.size());
        sessionDesc.searchPaths        = searchPaths;
        sessionDesc.searchPathCount    = 1;
        sessionDesc.fileSystem         = &fs;
        if (!macros.empty())
        {
            sessionDesc.preprocessorMacros     = macros.data();
            sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
        }
        sessionDesc.compilerOptionEntries    = coptions.data();
        sessionDesc.compilerOptionEntryCount = static_cast<uint32_t>(coptions.size());

        ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef())))
            return R::err({ErrorCode::eCompileError, "createSession failed"});

        std::string logText;

        ComPtr<slang::IBlob> diag;
        const std::string    logicalPath = modulePath.empty() ? (moduleName + ".slang") : modulePath;
        slang::IModule*      module =
            session->loadModuleFromSourceString(moduleName.c_str(), logicalPath.c_str(), moduleSource.c_str(),
                                                diag.writeRef());
        logText += blob_to_string(diag);
        if (!module)
            return R::err({ErrorCode::eCompileError, "loadModule failed:\n" + logText});

        // Compose module + all defined entry points, then link.
        std::vector<slang::IComponentType*> comps;
        comps.push_back(module);
        SlangInt32 epCount = module->getDefinedEntryPointCount();
        std::vector<ComPtr<slang::IEntryPoint>> eps;
        for (SlangInt32 i = 0; i < epCount; ++i)
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
        logText += blob_to_string(diag);
        if (!composed)
            return R::err({ErrorCode::eCompileError, "compose failed:\n" + logText});

        ComPtr<slang::IComponentType> linked;
        diag = nullptr;
        composed->link(linked.writeRef(), diag.writeRef());
        logText += blob_to_string(diag);
        if (!linked)
            return R::err({ErrorCode::eCompileError, "link failed:\n" + logText});

        slang::ProgramLayout* layout = linked->getLayout(0, nullptr);

        SlangCompileResult out;
        const SlangUInt epReflCount = layout ? layout->getEntryPointCount() : 0;
        for (SlangUInt i = 0; i < epReflCount; ++i)
        {
            slang::EntryPointReflection* er = layout->getEntryPointByIndex(i);
            SlangEntryPoint              outEp;
            outEp.name  = er && er->getName() ? er->getName() : "";
            outEp.stage = shader_stage_from_slang(er ? (int)er->getStage() : SLANG_STAGE_NONE);

            if (opt.emitSpirv && spirvIndex >= 0)
            {
                ComPtr<slang::IBlob> code;
                diag = nullptr;
                if (SLANG_SUCCEEDED(linked->getEntryPointCode(static_cast<SlangInt>(i), spirvIndex,
                                                              code.writeRef(), diag.writeRef())) &&
                    code && code->getBufferSize() > 0)
                {
                    const uint32_t* words = static_cast<const uint32_t*>(code->getBufferPointer());
                    outEp.spirv.assign(words, words + code->getBufferSize() / sizeof(uint32_t));
                }
                logText += blob_to_string(diag);
            }
            if (opt.emitWgsl && wgslIndex >= 0)
            {
                ComPtr<slang::IBlob> code;
                diag = nullptr;
                if (SLANG_SUCCEEDED(linked->getEntryPointCode(static_cast<SlangInt>(i), wgslIndex,
                                                              code.writeRef(), diag.writeRef())) &&
                    code)
                {
                    outEp.wgsl = blob_to_string(code);
                }
                logText += blob_to_string(diag);
            }

            out.entryPoints.push_back(std::move(outEp));
        }

        out.dependencies = fs.dependencies();
        out.log          = std::move(logText);
        return R::ok(std::move(out));
    }
} // namespace vshaderc
