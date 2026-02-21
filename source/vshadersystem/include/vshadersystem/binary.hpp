#pragma once

#include "vshadersystem/result.hpp"
#include "vshadersystem/types.hpp"

#include <string>
#include <vector>

namespace vshadersystem
{
    // Chunked .vshbin format
    //
    // Header (fixed 32 bytes):
    //
    // - magic[8]      : "VSHBIN\0\0"
    // - version u32   : format version
    // - flags u32     : reserved flags
    //                   low 8 bits store ShaderStage
    // - contentHash u64 : hash of source content
    // - spirvHash   u64 : hash of SPIR-V
    // - reserved padding to 32 bytes
    //
    // Chunks:
    //
    // [tag u32][size u32][payload bytes]
    //
    // Known tags:
    //
    // 'SPRV' : SPIR-V bytecode
    // 'REFL' : reflection info
    // 'MDES' : material description
    //
    // Unknown chunks are skipped for forward compatibility.
    //
    // The format can be extended in future versions,
    // e.g.:
    //
    // 'DEPS' : dependency list
    // 'DXIL' : DirectX backend
    // 'MSL ' : Metal backend
    //

    Result<std::vector<uint8_t>> write_vshbin(const ShaderBinary& bin);
    Result<ShaderBinary>         read_vshbin(const std::vector<uint8_t>& bytes);

    Result<void>         write_vshbin_file(const std::string& path, const ShaderBinary& bin);
    Result<ShaderBinary> read_vshbin_file(const std::string& path);
} // namespace vshadersystem
