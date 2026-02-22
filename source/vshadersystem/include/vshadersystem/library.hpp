#pragma once

#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <cstdint>
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
    Result<ShaderLibrary> read_vslib(const std::string& filePath);

    // Find a shader blob by (keyHash, stage). Returns empty span if not found.
    Result<std::vector<uint8_t>> extract_vslib_blob(const ShaderLibrary& lib, uint64_t keyHash, ShaderStage stage);
} // namespace vshadersystem
