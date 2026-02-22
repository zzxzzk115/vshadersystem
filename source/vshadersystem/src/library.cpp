#include "vshadersystem/library.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace vshadersystem
{
    static constexpr uint8_t  kMagic[8] = {'V', 'S', 'H', 'L', 'I', 'B', 0, 0};
    static constexpr uint32_t kVersion  = 2;
    static constexpr uint32_t kFlags    = 0;

#pragma pack(push, 1)
    struct FileHeader
    {
        uint8_t  magic[8];
        uint32_t version;
        uint32_t flags;
        uint32_t entryCount;
        uint32_t reserved0;
        uint64_t tocOffset;
        uint64_t tocSize;
        uint64_t keywordsOffset;
        uint64_t keywordsSize;
    };

    struct FileEntry
    {
        uint64_t keyHash;
        uint8_t  stage;
        uint8_t  reserved[7];
        uint64_t offset;
        uint64_t size;
    };
#pragma pack(pop)

    static Result<void> write_all(std::ofstream& f, const void* data, size_t size)
    {
        f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!f)
            return Result<void>::err({ErrorCode::eIO, "Failed to write file."});
        return Result<void>::ok();
    }

    static Result<void> read_all(std::ifstream& f, void* data, size_t size)
    {
        f.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
        if (!f)
            return Result<void>::err({ErrorCode::eIO, "Failed to read file."});
        return Result<void>::ok();
    }

    Result<void> write_vslib(const std::string&                     filePath,
                             const std::vector<ShaderLibraryEntry>& inEntries,
                             const std::vector<uint8_t>*            engineKeywordsVkw)
    {
        // Sort to make output deterministic.
        std::vector<ShaderLibraryEntry> entries = inEntries;
        std::sort(entries.begin(), entries.end(), [](const ShaderLibraryEntry& a, const ShaderLibraryEntry& b) {
            if (a.keyHash != b.keyHash)
                return a.keyHash < b.keyHash;
            return static_cast<uint8_t>(a.stage) < static_cast<uint8_t>(b.stage);
        });

        // Build TOC
        std::vector<FileEntry> toc;
        toc.reserve(entries.size());

        uint64_t             blobOffset = sizeof(FileHeader); // blobs start right after header
        std::vector<uint8_t> blobData;
        blobData.reserve(1024);

        for (const auto& e : entries)
        {
            if (e.stage == ShaderStage::eUnknown)
                return Result<void>::err({ErrorCode::eInvalidArgument, "VSHLIB entry has unknown shader stage."});
            if (e.keyHash == 0)
                return Result<void>::err(
                    {ErrorCode::eInvalidArgument, "VSHLIB entry has keyHash=0 (reserved/invalid)."});

            FileEntry fe {};
            fe.keyHash = e.keyHash;
            fe.stage   = static_cast<uint8_t>(e.stage);
            std::memset(fe.reserved, 0, sizeof(fe.reserved));
            fe.offset = blobOffset;
            fe.size   = static_cast<uint64_t>(e.blob.size());

            // append blob
            blobData.insert(blobData.end(), e.blob.begin(), e.blob.end());
            blobOffset += fe.size;

            toc.push_back(fe);
        }

        const uint64_t tocOffset = sizeof(FileHeader) + blobData.size();
        const uint64_t tocSize   = toc.size() * sizeof(FileEntry);

        const uint64_t keywordsOffset = tocOffset + tocSize;
        const uint64_t keywordsSize   = (engineKeywordsVkw && !engineKeywordsVkw->empty()) ?
                                            static_cast<uint64_t>(engineKeywordsVkw->size()) :
                                            0ull;

        FileHeader hdr {};
        std::memcpy(hdr.magic, kMagic, sizeof(kMagic));
        hdr.version        = kVersion;
        hdr.flags          = kFlags;
        hdr.entryCount     = static_cast<uint32_t>(toc.size());
        hdr.reserved0      = 0;
        hdr.tocOffset      = tocOffset;
        hdr.tocSize        = tocSize;
        hdr.keywordsOffset = (keywordsSize > 0) ? keywordsOffset : 0ull;
        hdr.keywordsSize   = keywordsSize;

        std::ofstream f(filePath, std::ios::binary);
        if (!f)
            return Result<void>::err({ErrorCode::eIO, "Failed to open output file: " + filePath});

        // write header
        {
            auto r = write_all(f, &hdr, sizeof(hdr));
            if (!r.isOk())
                return r;
        }

        // write blobs
        if (!blobData.empty())
        {
            auto r = write_all(f, blobData.data(), blobData.size());
            if (!r.isOk())
                return r;
        }

        // write toc
        if (!toc.empty())
        {
            auto r = write_all(f, toc.data(), toc.size() * sizeof(FileEntry));
            if (!r.isOk())
                return r;
        }

        // write optional engine keywords bytes
        if (keywordsSize > 0)
        {
            auto r = write_all(f, engineKeywordsVkw->data(), engineKeywordsVkw->size());
            if (!r.isOk())
                return r;
        }

        return Result<void>::ok();
    }

    Result<ShaderLibrary> read_vslib(const std::string& filePath)
    {
        std::ifstream f(filePath, std::ios::binary);
        if (!f)
            return Result<ShaderLibrary>::err({ErrorCode::eIO, "Failed to open file: " + filePath});

        FileHeader hdr {};
        {
            auto r = read_all(f, &hdr, sizeof(hdr));
            if (!r.isOk())
                return Result<ShaderLibrary>::err(r.error());
        }

        if (std::memcmp(hdr.magic, kMagic, sizeof(kMagic)) != 0)
            return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "Invalid VSHLIB magic."});
        if (hdr.version != kVersion)
            return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "Unsupported VSHLIB version."});

        // Determine file size
        f.seekg(0, std::ios::end);
        const uint64_t fileSize = static_cast<uint64_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        if (hdr.tocOffset + hdr.tocSize > fileSize)
            return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB TOC out of file range."});

        if (hdr.keywordsOffset != 0)
        {
            if (hdr.keywordsOffset + hdr.keywordsSize > fileSize)
                return Result<ShaderLibrary>::err(
                    {ErrorCode::eDeserializeError, "VSHLIB keywords chunk out of file range."});
            if (hdr.keywordsOffset < hdr.tocOffset + hdr.tocSize)
                return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB keywords chunk overlaps TOC."});
        }

        const uint64_t blobBegin = sizeof(FileHeader);
        const uint64_t blobEnd   = hdr.tocOffset;

        ShaderLibrary lib {};
        lib.blobData.resize(static_cast<size_t>(blobEnd - blobBegin));

        // Read blob region
        f.seekg(static_cast<std::streamoff>(blobBegin), std::ios::beg);
        if (!lib.blobData.empty())
        {
            auto r = read_all(f, lib.blobData.data(), lib.blobData.size());
            if (!r.isOk())
                return Result<ShaderLibrary>::err(r.error());
        }

        // Read TOC
        std::vector<FileEntry> toc;
        toc.resize(hdr.entryCount);

        f.seekg(static_cast<std::streamoff>(hdr.tocOffset), std::ios::beg);
        if (!toc.empty())
        {
            auto r = read_all(f, toc.data(), toc.size() * sizeof(FileEntry));
            if (!r.isOk())
                return Result<ShaderLibrary>::err(r.error());
        }

        lib.entries.reserve(toc.size());
        for (const auto& fe : toc)
        {
            ShaderLibraryTOCEntry e {};
            e.keyHash = fe.keyHash;
            e.stage   = static_cast<ShaderStage>(fe.stage);
            e.offset  = fe.offset;
            e.size    = fe.size;

            // Validate offsets relative to file
            if (e.offset < blobBegin || (e.offset + e.size) > blobEnd)
                return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB entry blob out of range."});

            lib.entries.push_back(e);
        }

        // Read optional engine keywords
        if (hdr.keywordsOffset != 0 && hdr.keywordsSize > 0)
        {
            lib.engineKeywordsVkw.resize(static_cast<size_t>(hdr.keywordsSize));
            f.seekg(static_cast<std::streamoff>(hdr.keywordsOffset), std::ios::beg);
            auto r = read_all(f, lib.engineKeywordsVkw.data(), lib.engineKeywordsVkw.size());
            if (!r.isOk())
                return Result<ShaderLibrary>::err(r.error());
        }

        return Result<ShaderLibrary>::ok(std::move(lib));
    }

    Result<std::vector<uint8_t>> extract_vslib_blob(const ShaderLibrary& lib, uint64_t keyHash, ShaderStage stage)
    {
        for (const auto& e : lib.entries)
        {
            if (e.keyHash == keyHash && e.stage == stage)
            {
                const uint64_t blobBegin = sizeof(FileHeader);
                const uint64_t rel       = e.offset - blobBegin;
                if (rel + e.size > lib.blobData.size())
                    return Result<std::vector<uint8_t>>::err(
                        {ErrorCode::eDeserializeError, "VSHLIB entry out of range."});

                std::vector<uint8_t> out;
                out.resize(static_cast<size_t>(e.size));
                std::memcpy(out.data(), lib.blobData.data() + rel, static_cast<size_t>(e.size));
                return Result<std::vector<uint8_t>>::ok(std::move(out));
            }
        }

        return Result<std::vector<uint8_t>>::err({ErrorCode::eIO, "VSHLIB entry not found."});
    }
} // namespace vshadersystem
