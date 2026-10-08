#include "cook_cache.hpp"

#include "vshadersystem/hash.hpp"

#include <slang.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace vshaderc::detail
{
    namespace fs = std::filesystem;
    namespace
    {
        constexpr uint64_t kMagic         = 0x3145484341435356; // VSCACHE1
        constexpr uint64_t kMaxCacheBytes = 512ull * 1024 * 1024;

        bool read_bytes(const fs::path& path, std::vector<uint8_t>& out)
        {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec))
                return false;
            const auto size = fs::file_size(path, ec);
            if (ec || size > kMaxCacheBytes)
                return false;
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;
            out.resize(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
            return bool(file);
        }

        void put(std::vector<uint8_t>& bytes, uint64_t n)
        {
            for (unsigned i = 0; i < 8; ++i)
                bytes.push_back(static_cast<uint8_t>(n >> (i * 8)));
        }
        bool take(const std::vector<uint8_t>& bytes, size_t& offset, uint64_t& n)
        {
            if (offset > bytes.size() || bytes.size() - offset < 8)
                return false;
            n = 0;
            for (unsigned i = 0; i < 8; ++i)
                n |= uint64_t(bytes[offset++]) << (i * 8);
            return true;
        }
        std::string hex(uint64_t n)
        {
            std::ostringstream text;
            text << std::hex << std::setw(16) << std::setfill('0') << n;
            return text.str();
        }
        void field(std::ostringstream& text, std::string_view value)
        {
            text << value.size() << ':';
            text.write(value.data(), static_cast<std::streamsize>(value.size()));
        }
        fs::path executable()
        {
#ifdef _WIN32
            std::wstring buffer(32768, L'\0');
            const auto   size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            buffer.resize(size);
            return buffer;
#elif defined(__APPLE__)
            uint32_t          size = 4096;
            std::vector<char> buffer(size);
            if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            {
                buffer.resize(size);
                if (_NSGetExecutablePath(buffer.data(), &size) != 0)
                    return {};
            }
            return fs::absolute(buffer.data());
#else
            std::error_code ec;
            return fs::read_symlink("/proc/self/exe", ec);
#endif
        }
        fs::path slang_library()
        {
#ifdef _WIN32
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(&spGetBuildTagString),
                                    &module))
                return {};
            std::wstring buffer(32768, L'\0');
            buffer.resize(GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size())));
            return buffer;
#else
            Dl_info info {};
            if (dladdr(reinterpret_cast<const void*>(&spGetBuildTagString), &info) && info.dli_fname)
                return fs::absolute(info.dli_fname);
            return {};
#endif
        }
        uint64_t hash_compiler_file(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                throw std::runtime_error("cannot fingerprint compiler file: " + path.string());
            XXH64_state_t* state = XXH64_createState();
            if (!state)
                throw std::bad_alloc();
            XXH64_reset(state, 0);
            char buffer[65536];
            while (file)
            {
                file.read(buffer, sizeof(buffer));
                XXH64_update(state, buffer, static_cast<size_t>(file.gcount()));
            }
            const auto hash = XXH64_digest(state);
            XXH64_freeState(state);
            if (!file.eof())
                throw std::runtime_error("cannot read compiler file: " + path.string());
            return hash;
        }
    } // namespace

    FileDependency observe_file(const fs::path& path)
    {
        std::vector<uint8_t> bytes;
        const auto           name = fs::absolute(path).lexically_normal().generic_string();
        if (!read_bytes(path, bytes))
            return {name, false, 0};
        return {name, true, vshadersystem::xxhash64(bytes.data(), bytes.size())};
    }

    bool atomic_write(const fs::path& path, const std::vector<uint8_t>& bytes)
    {
        std::vector<uint8_t> old;
        if (read_bytes(path, old) && old == bytes)
            return true; // keep mtime so downstream build steps also stay incremental
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        if (ec)
            return false;
        static std::atomic<uint64_t> serial {0};
#ifdef _WIN32
        const auto pid = GetCurrentProcessId();
#else
        const auto pid = getpid();
#endif
        fs::path temporary = path;
        temporary += ".tmp-" + std::to_string(pid) + "-" + std::to_string(serial.fetch_add(1));
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            file.close();
            if (!file)
            {
                fs::remove(temporary, ec);
                return false;
            }
        }
#ifdef _WIN32
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) == 0)
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
        fs::rename(temporary, path, ec);
#endif
        if (ec)
        {
            fs::remove(temporary, ec);
            return false;
        }
        return true;
    }

    std::string compiler_identity(bool nativeTargets)
    {
        std::set<fs::path> files;
        const auto         exe     = executable();
        const auto         library = slang_library();
        if (exe.empty() || library.empty())
            throw std::runtime_error("cannot locate compiler/Slang binary for cache identity");
        files.insert(exe);
        files.insert(library);
        for (const auto& directory : {exe.parent_path(), library.parent_path()})
        {
            for (const auto& item : fs::directory_iterator(directory))
            {
                const auto name = item.path().filename().string();
                if (name.starts_with("slang") || name.starts_with("libslang") || name.starts_with("dxcompiler") ||
                    name.starts_with("dxil"))
                {
                    if (item.is_directory())
                    {
                        for (const auto& child : fs::recursive_directory_iterator(item.path()))
                            if (child.is_regular_file())
                                files.insert(child.path());
                    }
                    else if (item.is_regular_file())
                        files.insert(item.path());
                }
            }
        }
        std::ostringstream identity;
        field(identity, "vshaderc-cache-v1");
        field(identity, spGetBuildTagString());
        for (const auto* variable : {"PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"})
        {
            const char* value = std::getenv(variable);
            field(identity, value ? value : "");
        }
        if (nativeTargets)
        {
            std::vector<fs::path> search {exe.parent_path(), library.parent_path(), fs::current_path()};
#ifdef _WIN32
            std::wstring system(32768, L'\0');
            system.resize(GetSystemDirectoryW(system.data(), static_cast<UINT>(system.size())));
            search.emplace_back(system);
#endif
            for (const char* variable : {"PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"})
            {
                const char*       env = std::getenv(variable);
                std::stringstream parts(env ? env : "");
                std::string       part;
                while (std::getline(parts,
                                    part,
#ifdef _WIN32
                                    ';'
#else
                                    ':'
#endif
                                    ))
                    if (!part.empty())
                        search.emplace_back(part);
            }
            for (const auto& directory : search)
                for (const auto* name : {"dxcompiler.dll",
                                         "dxil.dll",
                                         "d3dcompiler_47.dll",
                                         "fxc.exe",
                                         "dxc.exe",
                                         "libdxcompiler.so",
                                         "libdxcompiler.dylib",
                                         "libdxil.so",
                                         "libdxil.dylib"})
                {
                    const auto      path = fs::absolute(directory / name).lexically_normal();
                    std::error_code ec;
                    field(identity, path.generic_string());
                    field(identity, fs::is_regular_file(path, ec) ? "present" : "absent");
                    if (!ec && fs::is_regular_file(path))
                        files.insert(path);
                }
        }
        for (const auto& file : files)
        {
            field(identity, fs::absolute(file).lexically_normal().generic_string());
            field(identity, hex(hash_compiler_file(file)));
        }
        return hex(vshadersystem::xxhash64(identity.str()));
    }

    CookCache::CookCache(fs::path directory, std::string compilerIdentity) :
        m_directory(std::move(directory)), m_compilerIdentity(std::move(compilerIdentity))
    {}

    std::string CookCache::key(const fs::path&             path,
                               const std::string&          source,
                               const ShaderBuildOptions&   bo,
                               const std::vector<uint8_t>& keywords) const
    {
        std::ostringstream input;
        field(input, m_compilerIdentity);
        field(input, fs::current_path().generic_string());
        field(input, fs::absolute(path).lexically_normal().generic_string());
        field(input, bo.shaderId);
        field(input, hex(vshadersystem::xxhash64(source)));
        field(input, hex(vshadersystem::xxhash64(keywords.data(), keywords.size())));
        const auto& co = bo.compile;
        input << co.emitSpirv << co.emitWgsl << co.emitDxbc << co.emitDxil << co.debugInfo << co.optimize
              << int(co.matrixLayout) << ':' << bo.maxVariants << ':' << bo.skipInvalid;
        for (const auto& profile : {co.spirvProfile, co.dxbcProfile, co.dxilProfile})
            field(input, profile);
        field(input, "search-dirs");
        input << co.searchDirs.size() << ':';
        for (const auto& dir : co.searchDirs)
            field(input, fs::absolute(dir).lexically_normal().generic_string());
        field(input, "defines");
        input << co.defines.size() << ':';
        for (const auto& define : co.defines)
        {
            field(input, define.name);
            field(input, define.value);
        }
        field(input, "vfs");
        input << co.vfsFiles.size() << ':';
        for (const auto& file : co.vfsFiles)
        {
            field(input, file.path);
            field(input, file.text);
        }
        // jobs, logging and output path do not alter generated shader code.
        return hex(vshadersystem::xxhash64(input.str()));
    }

    bool CookCache::matches(const std::vector<FileDependency>& dependencies, bool refresh)
    {
        if (refresh)
            m_files.clear();
        for (const auto& dependency : dependencies)
        {
            auto found = m_files.find(dependency.path);
            if (found == m_files.end())
                found = m_files.emplace(dependency.path, observe_file(dependency.path)).first;
            if (found->second != dependency)
                return false;
        }
        return true;
    }

    bool CookCache::load(const std::string&                            key,
                         std::vector<vshadersystem::v1::LibraryEntry>& entries,
                         std::vector<FileDependency>&                  dependencies)
    {
        std::vector<uint8_t> bytes;
        if (!read_bytes(m_directory / (key + ".vshcache"), bytes) || bytes.size() < 32)
            return false;
        size_t   tail     = bytes.size() - 8;
        uint64_t checksum = 0;
        if (!take(bytes, tail, checksum) || checksum != vshadersystem::xxhash64(bytes.data(), bytes.size() - 8))
            return false;
        bytes.resize(bytes.size() - 8);
        size_t   offset = 0;
        uint64_t magic = 0, count = 0;
        if (!take(bytes, offset, magic) || magic != kMagic || !take(bytes, offset, count) || count > 100000)
            return false;
        std::vector<FileDependency> observed;
        for (uint64_t i = 0; i < count; ++i)
        {
            uint64_t length = 0, exists = 0, hash = 0;
            if (!take(bytes, offset, length) || length > 1024 * 1024 || length > bytes.size() - offset)
                return false;
            std::string path(reinterpret_cast<const char*>(bytes.data() + offset), static_cast<size_t>(length));
            offset += static_cast<size_t>(length);
            if (!take(bytes, offset, exists) || exists > 1 || !take(bytes, offset, hash))
                return false;
            observed.push_back({std::move(path), exists != 0, hash});
        }
        if (!matches(observed))
            return false;
        std::vector<uint8_t> payload(bytes.begin() + static_cast<ptrdiff_t>(offset), bytes.end());
        auto                 library = vshadersystem::v1::read_library(payload);
        if (!library.isOk())
            return false;
        entries      = std::move(library.value().entries);
        dependencies = std::move(observed);
        return true;
    }

    bool CookCache::save(const std::string&                                  key,
                         const std::vector<FileDependency>&                  dependencies,
                         const std::vector<vshadersystem::v1::LibraryEntry>& entries)
    {
        auto payload = vshadersystem::v1::write_library(entries);
        if (!payload.isOk())
            return false;
        std::vector<uint8_t> bytes;
        put(bytes, kMagic);
        put(bytes, dependencies.size());
        for (const auto& dependency : dependencies)
        {
            put(bytes, dependency.path.size());
            bytes.insert(bytes.end(), dependency.path.begin(), dependency.path.end());
            put(bytes, dependency.exists);
            put(bytes, dependency.contentHash);
        }
        bytes.insert(bytes.end(), payload.value().begin(), payload.value().end());
        put(bytes, vshadersystem::xxhash64(bytes.data(), bytes.size()));
        return atomic_write(m_directory / (key + ".vshcache"), bytes);
    }
} // namespace vshaderc::detail
