#pragma once

#include "vshaderc/slang_build.hpp"
#include "vshadersystem/vsh_format.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace vshaderc::detail
{
    // Cache format is private to the compiler; the runtime .vshlib format is unchanged.
    class CookCache
    {
    public:
        CookCache(std::filesystem::path directory, std::string compilerIdentity);
        std::string key(const std::filesystem::path& sourcePath,
                        const std::string&           source,
                        const ShaderBuildOptions&    options,
                        const std::vector<uint8_t>&  engineKeywords) const;
        bool        load(const std::string&                            key,
                         std::vector<vshadersystem::v1::LibraryEntry>& entries,
                         std::vector<FileDependency>&                  dependencies);
        bool        save(const std::string&                                  key,
                         const std::vector<FileDependency>&                  dependencies,
                         const std::vector<vshadersystem::v1::LibraryEntry>& entries);
        bool        matches(const std::vector<FileDependency>& dependencies, bool refresh = false);

    private:
        std::filesystem::path                           m_directory;
        std::string                                     m_compilerIdentity;
        std::unordered_map<std::string, FileDependency> m_files;
    };

    std::string    compiler_identity(bool nativeTargets);
    bool           atomic_write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes);
    FileDependency observe_file(const std::filesystem::path& path);
} // namespace vshaderc::detail
