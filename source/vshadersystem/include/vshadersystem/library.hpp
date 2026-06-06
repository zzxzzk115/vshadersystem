#pragma once

#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vshadersystem
{
    // ------------------------------------------------------------
    // .vshlib - Shader Library container
    //
    // This is a very small, deterministic container designed for:
    // - packaging many precompiled shader binaries (typically .vshbin)
    // - mapping them by a 64-bit key hash (e.g., VariantKey hash)
    //
    // File format (version 2):
    //
    // Header (fixed 48 bytes):
    // - magic[8]       : "VSHLIB\0\0"
    // - version u32    : 1
    // - flags u32      : reserved
    // - entryCount u32 : number of entries
    // - reserved u32   : reserved
    // - tocOffset u64      : offset of TOC
    // - tocSize u64        : size of TOC bytes
    // - keywordsOffset u64 : optional engine_keywords.vkw bytes offset (0 if absent)
    // - keywordsSize u64   : optional engine_keywords.vkw bytes size
    //
    // TOC (table of contents):
    // - entryCount * Entry
    // Entry:
    // - keyHash  u64
    // - stage    u8   (ShaderStage)
    // - reserved u8[7]
    // - offset   u64  (blob offset)
    // - size     u64  (blob size)
    //
    // Blobs:
    // - raw bytes for each shader binary (commonly .vshbin)
    // ------------------------------------------------------------

    struct ShaderLibraryEntry
    {
        uint64_t             keyHash = 0;
        ShaderStage          stage   = ShaderStage::eUnknown;
        std::vector<uint8_t> blob; // typically a .vshbin payload
    };

    struct ShaderLibraryTOCEntry
    {
        uint64_t    keyHash = 0;
        ShaderStage stage   = ShaderStage::eUnknown;
        uint64_t    offset  = 0;
        uint64_t    size    = 0;
    };

    struct ShaderLibrary
    {
        std::vector<ShaderLibraryTOCEntry> entries;
        std::vector<uint8_t>               blobData;          // concatenated blob storage
        std::vector<uint8_t>               engineKeywordsVkw; // optional raw bytes
    };

    Result<void> write_vslib(const std::string&                     filePath,
                             const std::vector<ShaderLibraryEntry>& entries,
                             const std::vector<uint8_t>*            engineKeywordsVkw = nullptr);

    inline Result<void> write_vslib(const std::string& filePath, const std::vector<ShaderLibraryEntry>& entries)
    {
        return write_vslib(filePath, entries, nullptr);
    }

    // Read the library file and return TOC + blob data.
    Result<ShaderLibrary> read_vshlib_file(const std::string& filePath);

    // Read the library data from a contiguous memory span.
    Result<ShaderLibrary> read_vshlib(std::span<const uint8_t> blob);

    // Find a shader blob by (keyHash, stage). Returns empty span if not found.
    Result<std::vector<uint8_t>> extract_vshlib_blob(const ShaderLibrary& lib, uint64_t keyHash, ShaderStage stage);

    // ------------------------------------------------------------
    // .vshglsl - GLSL include library (mountable into the compile VFS)
    //
    // A deterministic container of GLSL source files addressed by a relative
    // virtual path. Packaged with `vshaderc pack-glsl` and mounted at compile
    // time (`--mount <name>=<file.vshglsl>`), so `#include "<name>/<path>"`
    // resolves against the library from any source location.
    //
    // File format (version 1):
    //   magic[8]      : "VSHGLSL\0"
    //   version u32   : 1
    //   count   u32   : number of files
    //   count * { pathLen u32, path bytes, srcLen u32, src bytes }
    // ------------------------------------------------------------
    struct GlslLibraryFile
    {
        std::string virtualPath; // relative to the mount point, e.g. "common/pbr.glsl"
        std::string sourceText;
    };

    Result<void> write_glsl_library(const std::string& filePath, const std::vector<GlslLibraryFile>& files);
    Result<std::vector<GlslLibraryFile>> read_glsl_library_file(const std::string& filePath);
    Result<std::vector<GlslLibraryFile>> read_glsl_library(std::span<const uint8_t> blob);
} // namespace vshadersystem
