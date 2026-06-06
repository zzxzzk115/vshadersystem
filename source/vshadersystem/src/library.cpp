#include "vshadersystem/library.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>

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

    Result<ShaderLibrary> read_vshlib(std::span<const uint8_t> data)
    {
        if (data.size() < sizeof(FileHeader))
            return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB file too small."});

        FileHeader hdr {};
        std::memcpy(&hdr, data.data(), sizeof(FileHeader));

        if (std::memcmp(hdr.magic, kMagic, sizeof(kMagic)) != 0)
            return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "Invalid VSHLIB magic."});
        if (hdr.version != kVersion)
            return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "Unsupported VSHLIB version."});

        const uint64_t fileSize = static_cast<uint64_t>(data.size());

        if (hdr.tocOffset < sizeof(FileHeader) || (hdr.tocOffset + hdr.tocSize) > fileSize)
            return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB TOC out of file range."});

        const uint64_t expectedTocSize = static_cast<uint64_t>(sizeof(FileEntry)) * hdr.entryCount;
        if (hdr.tocSize != expectedTocSize)
            return Result<ShaderLibrary>::err({
                ErrorCode::eDeserializeError,
                "VSHLIB TOC size does not match entry count.",
            });

        const uint64_t tocEnd = hdr.tocOffset + hdr.tocSize;
        if (hdr.keywordsOffset != 0)
        {
            if ((hdr.keywordsOffset + hdr.keywordsSize) > fileSize)
                return Result<ShaderLibrary>::err(
                    {ErrorCode::eDeserializeError, "VSHLIB keywords chunk out of file range."});
            if (hdr.keywordsOffset < tocEnd)
                return Result<ShaderLibrary>::err(
                    {ErrorCode::eDeserializeError, "VSHLIB keywords chunk overlaps TOC."});
        }

        const uint64_t blobBegin = sizeof(FileHeader);
        const uint64_t blobEnd   = hdr.tocOffset;
        if (blobEnd < blobBegin)
            return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB blob region is invalid."});

        ShaderLibrary lib {};
        lib.blobData.resize(static_cast<size_t>(blobEnd - blobBegin));
        if (!lib.blobData.empty())
        {
            std::memcpy(lib.blobData.data(), data.data() + blobBegin, lib.blobData.size());
        }

        lib.entries.reserve(hdr.entryCount);
        for (uint32_t idx = 0; idx < hdr.entryCount; ++idx)
        {
            const uint64_t entryOffset = hdr.tocOffset + static_cast<uint64_t>(idx) * sizeof(FileEntry);
            if ((entryOffset + sizeof(FileEntry)) > fileSize)
                return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB TOC entry out of range."});

            FileEntry fe {};
            std::memcpy(&fe, data.data() + entryOffset, sizeof(FileEntry));

            ShaderLibraryTOCEntry e {};
            e.keyHash = fe.keyHash;
            e.stage   = static_cast<ShaderStage>(fe.stage);
            e.offset  = fe.offset;
            e.size    = fe.size;

            if (e.offset < blobBegin || e.offset > blobEnd)
                return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB entry blob out of range."});
            const uint64_t maxSize = blobEnd - e.offset;
            if (e.size > maxSize)
                return Result<ShaderLibrary>::err({ErrorCode::eDeserializeError, "VSHLIB entry blob out of range."});

            lib.entries.push_back(e);
        }

        if (hdr.keywordsOffset != 0 && hdr.keywordsSize > 0)
        {
            lib.engineKeywordsVkw.resize(static_cast<size_t>(hdr.keywordsSize));
            std::memcpy(lib.engineKeywordsVkw.data(), data.data() + hdr.keywordsOffset, lib.engineKeywordsVkw.size());
        }

        return Result<ShaderLibrary>::ok(std::move(lib));
    }

    Result<ShaderLibrary> read_vshlib_file(const std::string& filePath)
    {
        std::ifstream f(filePath, std::ios::binary | std::ios::ate);
        if (!f)
            return Result<ShaderLibrary>::err({ErrorCode::eIO, "Failed to open file: " + filePath});

        const std::streamsize streamSize = f.tellg();
        if (streamSize < 0)
            return Result<ShaderLibrary>::err({ErrorCode::eIO, "Failed to query file size: " + filePath});

        if (static_cast<uint64_t>(streamSize) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            return Result<ShaderLibrary>::err({ErrorCode::eIO, "VSHLIB file is too large."});

        std::vector<uint8_t> data(static_cast<size_t>(streamSize));
        f.seekg(0, std::ios::beg);
        if (streamSize > 0)
        {
            f.read(reinterpret_cast<char*>(data.data()), streamSize);
            if (!f)
                return Result<ShaderLibrary>::err({ErrorCode::eIO, "Failed to read file: " + filePath});
        }

        return read_vshlib(std::span<const uint8_t>(data.data(), data.size()));
    }

    Result<std::vector<uint8_t>> extract_vshlib_blob(const ShaderLibrary& lib, uint64_t keyHash, ShaderStage stage)
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

    // ------------------------------------------------------------
    // .vshglsl - GLSL include library
    // ------------------------------------------------------------
    namespace
    {
        constexpr uint8_t  kGlslMagic[8] = {'V', 'S', 'H', 'G', 'L', 'S', 'L', 0};
        constexpr uint32_t kGlslVersion  = 1;

        void append_u32(std::vector<uint8_t>& buf, uint32_t v)
        {
            buf.push_back(static_cast<uint8_t>(v & 0xFFu));
            buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
            buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
            buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
        }

        void append_bytes(std::vector<uint8_t>& buf, std::string_view s)
        {
            buf.insert(buf.end(), s.begin(), s.end());
        }

        bool read_u32(std::span<const uint8_t> blob, size_t& pos, uint32_t& out)
        {
            if (pos + 4 > blob.size())
                return false;
            out = static_cast<uint32_t>(blob[pos]) | (static_cast<uint32_t>(blob[pos + 1]) << 8) |
                  (static_cast<uint32_t>(blob[pos + 2]) << 16) | (static_cast<uint32_t>(blob[pos + 3]) << 24);
            pos += 4;
            return true;
        }
    } // namespace

    Result<void> write_glsl_library(const std::string& filePath, const std::vector<GlslLibraryFile>& inFiles)
    {
        // Deterministic ordering by virtualPath.
        std::vector<GlslLibraryFile> files = inFiles;
        std::sort(files.begin(), files.end(), [](const GlslLibraryFile& a, const GlslLibraryFile& b) {
            return a.virtualPath < b.virtualPath;
        });

        std::vector<uint8_t> buf;
        buf.insert(buf.end(), kGlslMagic, kGlslMagic + sizeof(kGlslMagic));
        append_u32(buf, kGlslVersion);
        append_u32(buf, static_cast<uint32_t>(files.size()));
        for (const auto& file : files)
        {
            if (file.virtualPath.empty())
                return Result<void>::err({ErrorCode::eInvalidArgument, "VSHGLSL file has empty virtualPath."});
            append_u32(buf, static_cast<uint32_t>(file.virtualPath.size()));
            append_bytes(buf, file.virtualPath);
            append_u32(buf, static_cast<uint32_t>(file.sourceText.size()));
            append_bytes(buf, file.sourceText);
        }

        std::ofstream f(filePath, std::ios::binary);
        if (!f)
            return Result<void>::err({ErrorCode::eIO, "Failed to open output file: " + filePath});
        return write_all(f, buf.data(), buf.size());
    }

    Result<std::vector<GlslLibraryFile>> read_glsl_library(std::span<const uint8_t> blob)
    {
        using R = Result<std::vector<GlslLibraryFile>>;
        if (blob.size() < sizeof(kGlslMagic) + 8 || std::memcmp(blob.data(), kGlslMagic, sizeof(kGlslMagic)) != 0)
            return R::err({ErrorCode::eParseError, "Not a VSHGLSL file (bad magic)."});

        size_t   pos = sizeof(kGlslMagic);
        uint32_t version = 0;
        uint32_t count   = 0;
        if (!read_u32(blob, pos, version) || version != kGlslVersion)
            return R::err({ErrorCode::eParseError, "Unsupported VSHGLSL version."});
        if (!read_u32(blob, pos, count))
            return R::err({ErrorCode::eParseError, "Truncated VSHGLSL header."});

        std::vector<GlslLibraryFile> files;
        files.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t pathLen = 0;
            if (!read_u32(blob, pos, pathLen) || pos + pathLen > blob.size())
                return R::err({ErrorCode::eParseError, "Truncated VSHGLSL path."});
            GlslLibraryFile file;
            file.virtualPath.assign(reinterpret_cast<const char*>(blob.data() + pos), pathLen);
            pos += pathLen;

            uint32_t srcLen = 0;
            if (!read_u32(blob, pos, srcLen) || pos + srcLen > blob.size())
                return R::err({ErrorCode::eParseError, "Truncated VSHGLSL source."});
            file.sourceText.assign(reinterpret_cast<const char*>(blob.data() + pos), srcLen);
            pos += srcLen;

            files.push_back(std::move(file));
        }
        return R::ok(std::move(files));
    }

    Result<std::vector<GlslLibraryFile>> read_glsl_library_file(const std::string& filePath)
    {
        std::ifstream f(filePath, std::ios::binary | std::ios::ate);
        if (!f)
            return Result<std::vector<GlslLibraryFile>>::err({ErrorCode::eIO, "Failed to open file: " + filePath});
        const auto size = static_cast<size_t>(f.tellg());
        f.seekg(0);
        std::vector<uint8_t> bytes(size);
        f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
        if (!f)
            return Result<std::vector<GlslLibraryFile>>::err({ErrorCode::eIO, "Failed to read file: " + filePath});
        return read_glsl_library(bytes);
    }
} // namespace vshadersystem
