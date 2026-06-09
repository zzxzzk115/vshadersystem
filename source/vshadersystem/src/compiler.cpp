#include "vshadersystem/compiler.hpp"
#include "vshadersystem/reflect.hpp"
#include "vshadersystem/result.hpp"

#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vshadersystem
{
    // ------------------------------------------------------------
    // glslang initialization (process-wide)
    // ------------------------------------------------------------
    static void ensure_glslang_initialized()
    {
        static const bool kGlslangInitialized = []() {
            glslang::InitializeProcess();
            return true;
        }();
        (void)kGlslangInitialized;
    }

    // ------------------------------------------------------------
    // Stage mapping helpers
    // ------------------------------------------------------------
    static EShLanguage to_esh_language(ShaderStage s)
    {
        switch (s)
        {
            case ShaderStage::eVert:
                return EShLangVertex;
            case ShaderStage::eFrag:
                return EShLangFragment;
            case ShaderStage::eGeom:
                return EShLangGeometry;
            case ShaderStage::eComp:
                return EShLangCompute;
            case ShaderStage::eTask:
                return EShLangTask;
            case ShaderStage::eMesh:
                return EShLangMesh;
            case ShaderStage::eRgen:
                return EShLangRayGen;
            case ShaderStage::eRmiss:
                return EShLangMiss;
            case ShaderStage::eRchit:
                return EShLangClosestHit;
            case ShaderStage::eRahit:
                return EShLangAnyHit;
            case ShaderStage::eRint:
                return EShLangIntersect;
            default:
                return EShLangFragment;
        }
    }

    static const char* stage_name(ShaderStage s)
    {
        switch (s)
        {
            case ShaderStage::eVert:
                return "vert";
            case ShaderStage::eFrag:
                return "frag";
            case ShaderStage::eGeom:
                return "geom";
            case ShaderStage::eComp:
                return "comp";
            case ShaderStage::eTask:
                return "task";
            case ShaderStage::eMesh:
                return "mesh";
            case ShaderStage::eRgen:
                return "rgen";
            case ShaderStage::eRmiss:
                return "rmiss";
            case ShaderStage::eRchit:
                return "rchit";
            case ShaderStage::eRahit:
                return "rahit";
            case ShaderStage::eRint:
                return "rint";
            default:
                return "unknown";
        }
    }

    // ------------------------------------------------------------
    // Default resources (portable across glslang packaging)
    //
    // We keep this close to common “reasonable defaults” and what engines typically use.
    // This mirrors the style used by Vulkan-oriented shader compilers.
    // ------------------------------------------------------------
    namespace
    {
        constexpr TBuiltInResource kDefaultResources {
            .maxLights                                 = 32,
            .maxClipPlanes                             = 6,
            .maxTextureUnits                           = 32,
            .maxTextureCoords                          = 32,
            .maxVertexAttribs                          = 64,
            .maxVertexUniformComponents                = 4096,
            .maxVaryingFloats                          = 64,
            .maxVertexTextureImageUnits                = 32,
            .maxCombinedTextureImageUnits              = 80,
            .maxTextureImageUnits                      = 32,
            .maxFragmentUniformComponents              = 4096,
            .maxDrawBuffers                            = 32,
            .maxVertexUniformVectors                   = 128,
            .maxVaryingVectors                         = 8,
            .maxFragmentUniformVectors                 = 16,
            .maxVertexOutputVectors                    = 16,
            .maxFragmentInputVectors                   = 15,
            .minProgramTexelOffset                     = -8,
            .maxProgramTexelOffset                     = 7,
            .maxClipDistances                          = 8,
            .maxComputeWorkGroupCountX                 = 65535,
            .maxComputeWorkGroupCountY                 = 65535,
            .maxComputeWorkGroupCountZ                 = 65535,
            .maxComputeWorkGroupSizeX                  = 1024,
            .maxComputeWorkGroupSizeY                  = 1024,
            .maxComputeWorkGroupSizeZ                  = 64,
            .maxComputeUniformComponents               = 1024,
            .maxComputeTextureImageUnits               = 16,
            .maxComputeImageUniforms                   = 8,
            .maxComputeAtomicCounters                  = 8,
            .maxComputeAtomicCounterBuffers            = 1,
            .maxVaryingComponents                      = 60,
            .maxVertexOutputComponents                 = 64,
            .maxGeometryInputComponents                = 64,
            .maxGeometryOutputComponents               = 128,
            .maxFragmentInputComponents                = 128,
            .maxImageUnits                             = 8,
            .maxCombinedImageUnitsAndFragmentOutputs   = 8,
            .maxCombinedShaderOutputResources          = 8,
            .maxImageSamples                           = 0,
            .maxVertexImageUniforms                    = 0,
            .maxTessControlImageUniforms               = 0,
            .maxTessEvaluationImageUniforms            = 0,
            .maxGeometryImageUniforms                  = 0,
            .maxFragmentImageUniforms                  = 8,
            .maxCombinedImageUniforms                  = 8,
            .maxGeometryTextureImageUnits              = 16,
            .maxGeometryOutputVertices                 = 256,
            .maxGeometryTotalOutputComponents          = 1024,
            .maxGeometryUniformComponents              = 1024,
            .maxGeometryVaryingComponents              = 64,
            .maxTessControlInputComponents             = 128,
            .maxTessControlOutputComponents            = 128,
            .maxTessControlTextureImageUnits           = 16,
            .maxTessControlUniformComponents           = 1024,
            .maxTessControlTotalOutputComponents       = 4096,
            .maxTessEvaluationInputComponents          = 128,
            .maxTessEvaluationOutputComponents         = 128,
            .maxTessEvaluationTextureImageUnits        = 16,
            .maxTessEvaluationUniformComponents        = 1024,
            .maxTessPatchComponents                    = 120,
            .maxPatchVertices                          = 32,
            .maxTessGenLevel                           = 64,
            .maxViewports                              = 16,
            .maxVertexAtomicCounters                   = 0,
            .maxTessControlAtomicCounters              = 0,
            .maxTessEvaluationAtomicCounters           = 0,
            .maxGeometryAtomicCounters                 = 0,
            .maxFragmentAtomicCounters                 = 8,
            .maxCombinedAtomicCounters                 = 8,
            .maxAtomicCounterBindings                  = 1,
            .maxVertexAtomicCounterBuffers             = 0,
            .maxTessControlAtomicCounterBuffers        = 0,
            .maxTessEvaluationAtomicCounterBuffers     = 0,
            .maxGeometryAtomicCounterBuffers           = 0,
            .maxFragmentAtomicCounterBuffers           = 1,
            .maxCombinedAtomicCounterBuffers           = 1,
            .maxAtomicCounterBufferSize                = 16384,
            .maxTransformFeedbackBuffers               = 4,
            .maxTransformFeedbackInterleavedComponents = 64,
            .maxCullDistances                          = 8,
            .maxCombinedClipAndCullDistances           = 8,
            .maxSamples                                = 4,

            .maxMeshOutputVerticesNV    = 256,
            .maxMeshOutputPrimitivesNV  = 512,
            .maxMeshWorkGroupSizeX_NV   = 32,
            .maxMeshWorkGroupSizeY_NV   = 1,
            .maxMeshWorkGroupSizeZ_NV   = 1,
            .maxTaskWorkGroupSizeX_NV   = 32,
            .maxTaskWorkGroupSizeY_NV   = 1,
            .maxTaskWorkGroupSizeZ_NV   = 1,
            .maxMeshViewCountNV         = 4,
            .maxMeshOutputVerticesEXT   = 256,
            .maxMeshOutputPrimitivesEXT = 512,
            .maxMeshWorkGroupSizeX_EXT  = 32,
            .maxMeshWorkGroupSizeY_EXT  = 1,
            .maxMeshWorkGroupSizeZ_EXT  = 1,
            .maxTaskWorkGroupSizeX_EXT  = 32,
            .maxTaskWorkGroupSizeY_EXT  = 1,
            .maxTaskWorkGroupSizeZ_EXT  = 1,
            .maxMeshViewCountEXT        = 4,

            .limits =
                {
                    .nonInductiveForLoops                 = true,
                    .whileLoops                           = true,
                    .doWhileLoops                         = true,
                    .generalUniformIndexing               = true,
                    .generalAttributeMatrixVectorIndexing = true,
                    .generalVaryingIndexing               = true,
                    .generalSamplerIndexing               = true,
                    .generalVariableIndexing              = true,
                    .generalConstantMatrixVectorIndexing  = true,
                },
        };

        // ------------------------------------------------------------
        // Small utilities
        // ------------------------------------------------------------
        bool read_text_file(const std::filesystem::path& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return false;

            f.seekg(0, std::ios::end);
            const std::streamoff size = f.tellg();
            if (size < 0)
                return false;

            f.seekg(0, std::ios::beg);

            out.resize(static_cast<size_t>(size));
            f.read(out.data(), size);

            f.close();

            return static_cast<bool>(f);
        }

        std::filesystem::path normalize_dep_path(const std::filesystem::path& p)
        {
            std::error_code ec;
            auto            canon = std::filesystem::weakly_canonical(p, ec);
            if (ec)
                return p;
            return canon;
        }

        std::string normalize_virtual_path(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (path.starts_with("./"))
                path.erase(0, 2);

            std::vector<std::string> parts;
            std::stringstream        ss(path);
            std::string              part;
            while (std::getline(ss, part, '/'))
            {
                if (part.empty() || part == ".")
                    continue;
                if (part == "..")
                {
                    if (!parts.empty())
                        parts.pop_back();
                    continue;
                }
                parts.push_back(std::move(part));
            }

            std::string out;
            for (const auto& item : parts)
            {
                if (!out.empty())
                    out.push_back('/');
                out += item;
            }
            return out;
        }

        // We build a preamble that:
        // - Enables include directives for glslang (#include "file")
        // - Enables cpp-style line directives for better error reporting
        // - Adds user defines
        std::string build_preamble(const CompileOptions& opt)
        {
            std::string preamble;
            preamble.reserve(256);

            // We require include support. Without this, glslang will not accept #include.
            preamble += "#extension GL_GOOGLE_include_directive : require\n";
            preamble += "#extension GL_GOOGLE_cpp_style_line_directive : require\n";

            for (const auto& d : opt.defines)
            {
                preamble += "#define ";
                preamble += d.name;
                if (!d.value.empty())
                {
                    preamble += " ";
                    preamble += d.value;
                }
                preamble += "\n";
            }

            return preamble;
        }

        std::string sanitize_path_component(std::string s)
        {
            for (auto& c : s)
            {
                if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                    c == '|')
                    c = '_';
            }
            return s;
        }

        void try_emit_intermediate_source(const SourceInput& input, const CompileOptions& opt)
        {
            if (opt.emitIntermediateDir.empty())
                return;

            std::error_code ec;
            std::filesystem::create_directories(opt.emitIntermediateDir, ec);
            if (ec)
                return;

            std::string base =
                input.virtualPath.empty() ? std::string("shader") : sanitize_path_component(input.virtualPath);
            base += ".";
            base += stage_name(opt.stage);
            base += ".final.glsl";

            const auto outPath = std::filesystem::path(opt.emitIntermediateDir) / base;

            std::ofstream f(outPath, std::ios::binary);
            if (!f)
                return;
            f.write(input.sourceText.data(), static_cast<std::streamsize>(input.sourceText.size()));
        }

        std::vector<std::string> split_lines(const std::string& s)
        {
            std::vector<std::string> out;
            std::istringstream       iss(s);
            std::string              line;
            while (std::getline(iss, line))
                out.push_back(line);
            // Preserve a trailing empty line if the source ends with '\n'
            if (!s.empty() && s.back() == '\n')
                out.push_back(std::string());
            return out;
        }

        std::string build_error_context_dump(const std::string&    stageLabel,
                                             const SourceInput&    input,
                                             const CompileOptions& opt,
                                             const std::string&    compilerLog)
        {
            if (!opt.dumpSourceOnError)
                return {};

            const auto lines = split_lines(input.sourceText);
            if (lines.empty())
                return {};

            // glslang typically uses: "ERROR: 0:37: ..." and sometimes "WARNING: 0:..."
            const std::regex re(R"((ERROR|WARNING):\s*(?:\d+|[^:\r\n]+):(\d+):\s*(.*))");

            std::ostringstream out;
            out << "\n---- SOURCE CONTEXT (" << stageLabel << ") ----\n";

            std::smatch m;
            std::string hay = compilerLog;
            bool        any = false;

            while (std::regex_search(hay, m, re))
            {
                any              = true;
                const int lineNo = std::max(1, std::stoi(m[2].str()));
                const int ctx    = std::max(0, opt.dumpContextLines);

                const int start = std::max(1, lineNo - ctx);
                const int end   = std::min(static_cast<int>(lines.size()), lineNo + ctx);

                out << m[1].str() << " @ line " << lineNo << ": " << m[3].str() << "\n";

                for (int i = start; i <= end; ++i)
                {
                    out << (i == lineNo ? "> " : "  ") << std::setw(4) << i << " | "
                        << lines[static_cast<size_t>(i - 1)] << "\n";
                }
                out << "\n";

                hay = m.suffix().str();
            }

            if (!any)
                return {};

            out << "--------------------------------\n";
            return out.str();
        }

        // ------------------------------------------------------------
        // Recording includer (no StandAlone dependency)
        //
        // Resolution strategy:
        // 1) If headerName is absolute and exists -> use it.
        // 2) If includerName looks like a file path -> try includer directory first (relative include behavior).
        // 3) Try rootDir (virtual file's parent) + opt.includeDirs in order.
        //
        // Dependencies are stored as canonical-ish absolute paths where possible.
        // ------------------------------------------------------------
        class RecordingIncluder final : public glslang::TShader::Includer
        {
        public:
            RecordingIncluder(std::filesystem::path              rootFilePath,
                              std::vector<std::string>           extraIncludeDirs,
                              std::vector<VirtualIncludeFile>     virtualIncludeFiles,
                              const std::vector<VfsMount>&        vfsMounts) :
                m_RootFilePath(std::move(rootFilePath))
            {
                for (auto& file : virtualIncludeFiles)
                {
                    if (file.virtualPath.empty())
                        continue;
                    m_VirtualFiles[normalize_virtual_path(std::move(file.virtualPath))] = std::move(file.sourceText);
                }

                // Flatten VFS mounts into the virtual file table by their absolute
                // VFS path: "<mount>/<file.virtualPath>" (normalized). These are the
                // authoritative include paths used from any source location.
                for (const auto& m : vfsMounts)
                {
                    for (const auto& file : m.files)
                    {
                        if (file.virtualPath.empty())
                            continue;
                        const auto absolute =
                            m.mount.empty() ? file.virtualPath : (m.mount + "/" + file.virtualPath);
                        m_VirtualFiles[normalize_virtual_path(absolute)] = file.sourceText;
                    }
                }

                // Root file directory (highest priority)
                if (!m_RootFilePath.empty())
                {
                    auto parent = m_RootFilePath.parent_path();
                    if (!parent.empty())
                        m_SearchDirs.push_back(parent);
                }

                // User include dirs (next)
                for (const auto& d : extraIncludeDirs)
                {
                    if (!d.empty())
                        m_SearchDirs.push_back(std::filesystem::path(d));
                }
            }

            const std::vector<std::string>& dependencies() const { return m_Dependencies; }

            IncludeResult*
            includeSystem(const char* headerName, const char* includerName, size_t /*inclusionDepth*/) override
            {
                return includeImpl(headerName, includerName);
            }

            IncludeResult*
            includeLocal(const char* headerName, const char* includerName, size_t /*inclusionDepth*/) override
            {
                return includeImpl(headerName, includerName);
            }

            void releaseInclude(IncludeResult* result) override
            {
                if (!result)
                    return;
                delete[] static_cast<char*>(result->userData);
                delete result;
            }

        private:
            IncludeResult* includeImpl(const char* headerName, const char* includerName)
            {
                if (!headerName || !*headerName)
                    return nullptr;

                if (auto* virtualFile = resolveVirtual(headerName, includerName))
                    return makeVirtualIncludeResult(virtualFile->first, virtualFile->second);

                std::filesystem::path resolved;
                if (!resolve(headerName, includerName, resolved))
                    return nullptr;

                std::string content;
                if (!read_text_file(resolved, content))
                    return nullptr;

                // Record dependency
                {
                    const auto norm = normalize_dep_path(resolved).string();
                    if (m_DepSet.insert(norm).second)
                        m_Dependencies.push_back(norm);
                }

                // Keep file content alive until releaseInclude()
                const size_t n    = content.size();
                char*        data = new char[n + 1];
                std::memcpy(data, content.data(), n);
                data[n] = '\0';

                // Store resolved path as "headerName" for better diagnostics
                return new IncludeResult(resolved.string(), data, n, data);
            }

            std::pair<const std::string, std::string>* resolveVirtual(const char* headerName, const char* includerName)
            {
                const auto req = normalize_virtual_path(headerName);
                if (req.empty())
                    return nullptr;

                // VFS semantics: the include path is an ABSOLUTE VFS path. Match it
                // exactly first so that e.g. `#include "vultra/mesh_material.glsl"`
                // resolves identically from any source location (root, generated
                // subdir, or a mounted library).
                if (auto it = m_VirtualFiles.find(req); it != m_VirtualFiles.end())
                    return &*it;

                // Fallback: resolve relative to the including file's directory (a
                // convenience for co-located helper includes).
                if (includerName && *includerName)
                {
                    const std::filesystem::path inc(normalize_virtual_path(includerName));
                    const auto                  parent = inc.parent_path().generic_string();
                    if (!parent.empty())
                    {
                        const auto relative = normalize_virtual_path(parent + "/" + req);
                        if (auto it = m_VirtualFiles.find(relative); it != m_VirtualFiles.end())
                            return &*it;
                    }
                }

                return nullptr;
            }

            IncludeResult* makeVirtualIncludeResult(const std::string& resolved, const std::string& content)
            {
                if (m_DepSet.insert(resolved).second)
                    m_Dependencies.push_back(resolved);

                const size_t n    = content.size();
                char*        data = new char[n + 1];
                std::memcpy(data, content.data(), n);
                data[n] = '\0';
                return new IncludeResult(resolved, data, n, data);
            }

            bool resolve(const char* headerName, const char* includerName, std::filesystem::path& out)
            {
                std::filesystem::path req(headerName);

                // Absolute include path
                if (req.is_absolute())
                {
                    if (std::filesystem::exists(req))
                    {
                        out = req;
                        return true;
                    }
                    return false;
                }

                // Relative include: try includer directory first if it looks like a path
                if (includerName && *includerName)
                {
                    std::filesystem::path inc(includerName);
                    // If includerName is a file, use its parent; if it's already a dir, use it directly.
                    std::filesystem::path base = inc.has_extension() ? inc.parent_path() : inc;
                    if (!base.empty())
                    {
                        std::filesystem::path candidate = base / req;
                        if (std::filesystem::exists(candidate))
                        {
                            out = candidate;
                            return true;
                        }
                    }
                }

                // Search additional include dirs
                for (const auto& dir : m_SearchDirs)
                {
                    std::filesystem::path candidate = dir / req;
                    if (std::filesystem::exists(candidate))
                    {
                        out = candidate;
                        return true;
                    }
                }

                return false;
            }

        private:
            std::filesystem::path              m_RootFilePath;
            std::vector<std::filesystem::path> m_SearchDirs;
            std::unordered_map<std::string, std::string> m_VirtualFiles;

            std::vector<std::string>        m_Dependencies;
            std::unordered_set<std::string> m_DepSet;
        };
    } // namespace

    // ------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------
    Result<CompileOutput> compile_glsl_to_spirv(const SourceInput& input, const CompileOptions& opt)
    {
        ensure_glslang_initialized();

        if (opt.emitIntermediateAlways)
            try_emit_intermediate_source(input, opt);

        if (input.virtualPath.empty())
        {
            return Result<CompileOutput>::err({ErrorCode::eInvalidArgument, "virtualPath must not be empty."});
        }

        const EShLanguage stage = to_esh_language(opt.stage);

        glslang::TShader shader(stage);

        // Give glslang a stable "file name" for diagnostics and includerName.
        const char* strings[] = {input.sourceText.c_str()};
        const int   lengths[] = {static_cast<int>(input.sourceText.size())};
        const char* names[]   = {input.virtualPath.c_str()};
        shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);

        shader.setEntryPoint("main");
        shader.setSourceEntryPoint("main");

        // Shader version: we keep both override + parse defaultVersion consistent.
        shader.setOverrideVersion(460);

        // Target environment (compile target only; no runtime Vulkan dependency).
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
        const auto targetVulkanEnv = opt.webgpuProfile ? glslang::EShTargetVulkan_1_1 : glslang::EShTargetVulkan_1_2;
        const auto targetSpvEnv    = opt.webgpuProfile ? glslang::EShTargetSpv_1_3 : glslang::EShTargetSpv_1_5;
        shader.setEnvClient(glslang::EShClientVulkan, targetVulkanEnv);
        shader.setEnvTarget(glslang::EShTargetSpv, targetSpvEnv);

        // Preamble: include directives + defines.
        const std::string preamble = build_preamble(opt);
        shader.setPreamble(preamble.empty() ? nullptr : preamble.c_str());

        // Include + dependency recording.
        RecordingIncluder includer(std::filesystem::path(input.virtualPath),
                                   opt.includeDirs,
                                   opt.virtualIncludeFiles,
                                   opt.vfsMounts);

        // Messages: keep Vulkan/SPIR-V rules. Cascading errors improves logs.
        constexpr auto kMessages =
            EShMessages {EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules | EShMsgCascadingErrors
#ifndef NDEBUG
                         | EShMsgKeepUncalled
#if 0
                  | EShMsgDebugInfo
#endif
#endif
                         | EShMsgEnhanced};

        // Parse
        if (!shader.parse(&kDefaultResources, 110, false, kMessages, includer))
        {
            std::string log;
            log += "glslang parse failed for stage ";
            log += stage_name(opt.stage);
            log += ":\n";
            log += shader.getInfoLog();
            log += shader.getInfoDebugLog();

            // Append source context dump (and optionally write intermediate source).
            if (opt.dumpSourceOnError)
                log += build_error_context_dump(stage_name(opt.stage), input, opt, log);
            try_emit_intermediate_source(input, opt);

            return Result<CompileOutput>::err({ErrorCode::eCompileError, std::move(log)});
        }

        // Link (single-stage program is fine; link is still required for some validation paths)
        glslang::TProgram program;
        program.addShader(&shader);

        if (!program.link(kMessages))
        {
            std::string log;
            log += "glslang link failed for stage ";
            log += stage_name(opt.stage);
            log += ":\n";
            log += program.getInfoLog();
            log += program.getInfoDebugLog();

            if (opt.dumpSourceOnError)
                log += build_error_context_dump(stage_name(opt.stage), input, opt, log);
            try_emit_intermediate_source(input, opt);

            return Result<CompileOutput>::err({ErrorCode::eCompileError, std::move(log)});
        }

        glslang::TIntermediate* intermediate = program.getIntermediate(stage);
        if (!intermediate)
        {
            return Result<CompileOutput>::err(
                {ErrorCode::eCompileError, "glslang did not produce an intermediate representation."});
        }

        // SPIR-V generation
        spv::SpvBuildLogger logger;
        glslang::SpvOptions spvOptions;
        spvOptions.disableOptimizer  = !opt.optimize;
        spvOptions.generateDebugInfo = opt.debugInfo;
        spvOptions.stripDebugInfo    = opt.stripDebugInfo;

        std::vector<uint32_t> spirv;
        glslang::GlslangToSpv(*intermediate, spirv, &logger, &spvOptions);

        CompileOutput out;
        out.spirv        = std::move(spirv);
        out.infoLog      = logger.getAllMessages();
        out.dependencies = includer.dependencies();

        auto reflection = reflect_spirv(out.spirv);
        if (!reflection.isOk())
            return Result<CompileOutput>::err(reflection.error());
        out.reflection = std::move(reflection.value());

        return Result<CompileOutput>::ok(std::move(out));
    }
} // namespace vshadersystem
