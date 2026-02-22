#pragma once

#include "vshadersystem/hash.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace vshadersystem
{
    // ------------------------------------------------------------
    // Shader ID
    //
    // A stable logical identifier for a shader used at runtime.
    // By default, we derive it from the virtual path:
    //   shaders/pbr.frag.vshader -> "pbr.frag"
    //
    // This avoids requiring developers to know internal source hashes.
    // ------------------------------------------------------------

    inline std::string shader_id_from_virtual_path(std::string_view virtualPath)
    {
        // shaders/pbr.frag.vshader -> "pbr.frag"
        auto path = std::filesystem::path(virtualPath);
        auto name = path.stem().string(); // "pbr.frag"
        return name;
    }

    inline uint64_t shader_id_hash(std::string_view shaderId) { return xxhash64(shaderId); }

    inline uint64_t shader_id_hash_from_virtual_path(std::string_view virtualPath)
    {
        const std::string id = shader_id_from_virtual_path(virtualPath);
        return shader_id_hash(id);
    }
} // namespace vshadersystem
