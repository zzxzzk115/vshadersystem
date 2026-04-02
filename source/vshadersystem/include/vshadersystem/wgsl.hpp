#pragma once

#include "vshadersystem/result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vshadersystem
{
    // Convert SPIR-V words to WGSL source using Tint.
    Result<std::string> spirv_to_wgsl(const std::vector<uint32_t>& spirv);
}
