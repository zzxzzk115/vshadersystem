#pragma once

// Build orchestrator: expands a .slang shader's permutation keywords into variants,
// compiles + reflects each, and assigns the variantHash used as the library lookup key.
// Reuses the engine-agnostic keyword/variant machinery (KeywordDecl, VariantKey,
// only_if constraints, engine .vkw keywords) from the runtime library.

#include "vshaderc/slang_compiler.hpp"

#include "vshadersystem/engine_keywords.hpp"
#include "vshadersystem/types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace vshaderc
{
    // One compiled (stage x permutation) variant of a shader.
    struct VariantBinary
    {
        vshadersystem::ShaderStage stage = vshadersystem::ShaderStage::eUnknown;
        std::string                entryPointName;

        // variantHash = hash(shaderIdHash, stage, resolved permute keyword values).
        uint64_t                                       variantHash = 0;
        std::vector<std::pair<std::string, uint32_t>>  keywordValues;

        std::vector<uint32_t>              spirv;
        std::string                        wgsl;
        std::vector<uint8_t>               dxbc;
        std::vector<uint8_t>               dxil;
        vshadersystem::ShaderReflection    reflection;
        vshadersystem::MaterialDescription material;
    };

    struct ShaderBuildOptions
    {
        SlangCompileOptions compile;  // VFS, search dirs, emitWgsl, profile, etc.
        std::string         shaderId; // stable logical id (drives shaderIdHash)

        // Optional engine-wide keywords (.vkw): their permute decls are expanded too.
        const vshadersystem::EngineKeywordsFile* engineKeywords = nullptr;

        // Skip variants whose only_if(...) constraint evaluates false.
        bool skipInvalid = true;

        // Safety cap on total permutation combinations (product of keyword domains).
        uint32_t maxVariants = 1u << 14;
    };

    struct ShaderBuildResult
    {
        std::string                shaderId;
        uint64_t                   shaderIdHash = 0;
        std::vector<VariantBinary> variants; // (stage x permutation)
        // The shader's own keyword declarations (same across all variants), carried into
        // each binary so a loaded library can enumerate toggleable features.
        std::vector<vshadersystem::KeywordDecl> keywords;
        uint32_t                                combinations = 0;
        uint32_t                                skipped      = 0;
        std::string                             log;
    };

    Result<ShaderBuildResult> build_shader(SlangCompiler&            compiler,
                                           const std::string&        moduleName,
                                           const std::string&        modulePath,
                                           const std::string&        moduleSource,
                                           const ShaderBuildOptions& opt);

    // Convert a built variant into the serializable runtime ShaderBinary (computes
    // spirvHash, carries entry point name + keyword decls). `shaderIdHash` and
    // `keywords` come from ShaderBuildResult.
    vshadersystem::ShaderBinary to_shader_binary(const VariantBinary&                            v,
                                                 uint64_t                                        shaderIdHash,
                                                 const std::vector<vshadersystem::KeywordDecl>& keywords = {});
} // namespace vshaderc
