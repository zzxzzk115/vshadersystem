#pragma once

// vshadersystem v1.0 on-disk formats (clean break from the legacy .vshbin/.vshlib).
//
// A single binary carries BOTH SPIR-V and (optionally) WGSL emitted from the same Slang
// source, so there is no separate WebGPU format: WebGPU consumers just read the WGSL
// chunk. The library is a TOC keyed by (variantHash, stage). A source pack bundles
// .slang library sources for compile-time `import` reuse.
//
// Zero Slang dependency: the writer is used by the offline compiler (vshaderc-lib) and
// the reader by engines linking the runtime library.

#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vshadersystem::v1
{
    // ---- single binary (.vshbin) ----
    Result<std::vector<uint8_t>> write_binary(const ShaderBinary& bin);
    Result<ShaderBinary>         read_binary(const std::vector<uint8_t>& bytes);

    // ---- library (.vshlib) ----
    struct LibraryEntry
    {
        uint64_t             variantHash = 0;
        ShaderStage          stage       = ShaderStage::eUnknown;
        std::vector<uint8_t> blob; // a write_binary() payload
    };

    struct Library
    {
        std::vector<LibraryEntry> entries;
        std::vector<uint8_t>      engineKeywords; // embedded .vkw bytes (may be empty)
    };

    // Entries are sorted by (variantHash, stage) for deterministic output.
    Result<std::vector<uint8_t>> write_library(const std::vector<LibraryEntry>& entries,
                                               const std::vector<uint8_t>*      engineKeywords = nullptr);
    Result<Library>              read_library(const std::vector<uint8_t>& bytes);

    // Returns the blob for (variantHash, stage), or nullptr if absent.
    const std::vector<uint8_t>* find(const Library& lib, uint64_t variantHash, ShaderStage stage);

    // ---- source pack (.vshslang): packaged .slang sources for import reuse ----
    struct SourceFile
    {
        std::string path; // logical path, forward slashes (e.g. "common/pbr.slang")
        std::string text;
    };

    Result<std::vector<uint8_t>>     write_source_pack(const std::vector<SourceFile>& files);
    Result<std::vector<SourceFile>> read_source_pack(const std::vector<uint8_t>& bytes);
} // namespace vshadersystem::v1
