#pragma once

// Shared helpers for the vshadersystem test suite. Tests author Slang inline, compile
// it through the real pipeline, and assert on the produced binaries / reflection /
// material / variants -- covering engine shader needs from trivial to complex.

#include <vshaderc/slang_build.hpp>
#include <vshaderc/slang_compiler.hpp>
#include <vshaderc/slang_metadata.hpp>
#include <vshaderc/slang_reflect.hpp>
#include <vshaderc/slang_validate.hpp>

#include <vshadersystem/types.hpp>

#include <string>
#include <vector>

namespace vsht
{
    using namespace vshadersystem;

    // A process-wide Slang compiler (creating a global session per test is slow).
    inline vshaderc::SlangCompiler& compiler()
    {
        static vshaderc::SlangCompiler c;
        return c;
    }

    inline vshaderc::SlangCompileOptions opts(bool wgsl = true)
    {
        vshaderc::SlangCompileOptions o;
        o.emitWgsl = wgsl;
        return o;
    }

    // Compile inline Slang; returns the per-entry-point result (throws-free, returns
    // Result). `extra` lets a test add VFS files (shared modules).
    inline Result<vshaderc::SlangCompileResult> compile(const std::string&                          src,
                                                        std::vector<vshaderc::SlangSourceFile>      extra = {},
                                                        vshaderc::SlangCompileOptions               o     = opts())
    {
        o.vfsFiles.insert(o.vfsFiles.end(), extra.begin(), extra.end());
        return compiler().compileModule("test", "test.slang", src, o);
    }

    inline Result<vshaderc::ShaderMetadata> metadata(const std::string& src)
    {
        return vshaderc::extract_shader_metadata(compiler(), "test", "test.slang", src, opts());
    }

    inline Result<vshaderc::ProgramReflection> reflect(const std::string& src)
    {
        auto m = metadata(src);
        vshaderc::ShaderMetadata meta = m.isOk() ? m.value() : vshaderc::ShaderMetadata{};
        return vshaderc::reflect_shader(compiler(), "test", "test.slang", src, opts(), meta);
    }

    inline Result<vshaderc::ShaderBuildResult> build(const std::string&                       src,
                                                     const std::string&                       id = "test/shader",
                                                     const EngineKeywordsFile*                engine = nullptr,
                                                     std::vector<vshaderc::SlangSourceFile>   extra  = {})
    {
        vshaderc::ShaderBuildOptions bo;
        bo.shaderId         = id;
        bo.compile          = opts();
        bo.compile.vfsFiles = std::move(extra);
        bo.engineKeywords   = engine;
        return vshaderc::build_shader(compiler(), "test", "test.slang", src, bo);
    }

    // Find the first entry point with the given stage in a compile result.
    inline const vshaderc::SlangEntryPoint* entry(const vshaderc::SlangCompileResult& r, ShaderStage s)
    {
        for (const auto& e : r.entryPoints)
            if (e.stage == s)
                return &e;
        return nullptr;
    }

    // Count descriptors of a given kind in a reflection.
    inline int count_kind(const ShaderReflection& refl, DescriptorKind k)
    {
        int n = 0;
        for (const auto& d : refl.descriptors)
            if (d.kind == k)
                ++n;
        return n;
    }

    inline const DescriptorBinding* descriptor(const ShaderReflection& refl, const std::string& name)
    {
        for (const auto& d : refl.descriptors)
            if (d.name == name)
                return &d;
        return nullptr;
    }
} // namespace vsht
