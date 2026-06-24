#pragma once

// In-memory ISlangFileSystem adapter: serves the packaged shader library + the builtin
// vsh module to Slang's `import`/`#include` resolution, falling back to real disk for
// the configured search directories. Records every file it serves so the build can
// track dependencies for caching.

#include <slang-com-ptr.h>
#include <slang.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace vshaderc::detail
{
    inline std::string normalize_vfs_path(std::string p)
    {
        for (auto& c : p)
            if (c == '\\')
                c = '/';
        // strip a single leading "./"
        while (p.size() >= 2 && p[0] == '.' && p[1] == '/')
            p.erase(0, 2);
        // strip leading slashes
        size_t i = 0;
        while (i < p.size() && p[i] == '/')
            ++i;
        if (i)
            p.erase(0, i);
        return p;
    }

    // A simple ISlangBlob backed by a std::string.
    class StringBlob final : public ISlangBlob
    {
    public:
        explicit StringBlob(std::string data) : m_Data(std::move(data)) {}

        SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override
        {
            if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangBlob::getTypeGuid())
            {
                addRef();
                *outObject = static_cast<ISlangBlob*>(this);
                return SLANG_OK;
            }
            return SLANG_E_NO_INTERFACE;
        }
        SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++m_Ref; }
        SLANG_NO_THROW uint32_t SLANG_MCALL release() override
        {
            uint32_t r = --m_Ref;
            if (r == 0)
                delete this;
            return r;
        }
        SLANG_NO_THROW void const* SLANG_MCALL getBufferPointer() override { return m_Data.data(); }
        SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override { return m_Data.size(); }

    private:
        std::string m_Data;
        uint32_t    m_Ref = 1;
    };

    // Custom basic file system. Slang wraps it and handles search-path combination by
    // calling loadFile() with candidate paths; we resolve them against the in-memory
    // map first, then disk.
    class MemoryFileSystem final : public ISlangFileSystem
    {
    public:
        void addFile(const std::string& path, std::string text)
        {
            m_Files[normalize_vfs_path(path)] = std::move(text);
        }
        void addSearchDir(const std::string& dir) { m_SearchDirs.push_back(dir); }

        const std::vector<std::string>& dependencies() const { return m_Deps; }

        // --- ISlangUnknown / ISlangCastable ---
        SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override
        {
            if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangCastable::getTypeGuid() ||
                uuid == ISlangFileSystem::getTypeGuid())
            {
                *outObject = static_cast<ISlangFileSystem*>(this);
                return SLANG_OK;
            }
            return SLANG_E_NO_INTERFACE;
        }
        SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 2; }   // non-owning; lifetime managed by caller
        SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }
        SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) override
        {
            if (guid == ISlangFileSystem::getTypeGuid() || guid == ISlangCastable::getTypeGuid() ||
                guid == ISlangUnknown::getTypeGuid())
                return static_cast<ISlangFileSystem*>(this);
            return nullptr;
        }

        // --- ISlangFileSystem ---
        SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(char const* path, ISlangBlob** outBlob) override
        {
            if (!path || !outBlob)
                return SLANG_E_INVALID_ARG;
            const std::string norm = normalize_vfs_path(path);

            // 1) exact in-memory match
            if (auto it = m_Files.find(norm); it != m_Files.end())
            {
                recordDep(norm);
                *outBlob = new StringBlob(it->second);
                return SLANG_OK;
            }
            // 2) suffix match (Slang may prepend a search dir we don't know about)
            for (const auto& [key, text] : m_Files)
            {
                if (norm.size() >= key.size() &&
                    norm.compare(norm.size() - key.size(), key.size(), key) == 0 &&
                    (norm.size() == key.size() || norm[norm.size() - key.size() - 1] == '/'))
                {
                    recordDep(key);
                    *outBlob = new StringBlob(text);
                    return SLANG_OK;
                }
            }
            // 3) disk fallback (path as-is, then each search dir)
            if (auto blob = tryDisk(path))
            {
                recordDep(path);
                *outBlob = blob;
                return SLANG_OK;
            }
            for (const auto& dir : m_SearchDirs)
            {
                std::string full = dir;
                if (!full.empty() && full.back() != '/' && full.back() != '\\')
                    full += '/';
                full += path;
                if (auto blob = tryDisk(full.c_str()))
                {
                    recordDep(full);
                    *outBlob = blob;
                    return SLANG_OK;
                }
            }
            return SLANG_E_NOT_FOUND;
        }

    private:
        void recordDep(const std::string& p)
        {
            if (std::find(m_Deps.begin(), m_Deps.end(), p) == m_Deps.end())
                m_Deps.push_back(p);
        }

        static ISlangBlob* tryDisk(const char* path)
        {
            FILE* f = std::fopen(path, "rb");
            if (!f)
                return nullptr;
            std::string data;
            std::fseek(f, 0, SEEK_END);
            long sz = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (sz > 0)
            {
                data.resize(static_cast<size_t>(sz));
                size_t rd = std::fread(data.data(), 1, data.size(), f);
                data.resize(rd);
            }
            std::fclose(f);
            return new StringBlob(std::move(data));
        }

        std::unordered_map<std::string, std::string> m_Files;
        std::vector<std::string>                     m_SearchDirs;
        std::vector<std::string>                     m_Deps;
    };
} // namespace vshaderc::detail
