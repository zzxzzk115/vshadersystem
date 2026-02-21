#pragma once

#include "result.hpp"
#include "types.hpp"

#include <vector>

namespace vshadersystem
{
    struct ReflectionOptions
    {
        bool includeBlockMembers  = true;
        bool includePushConstants = true;
    };

    Result<ShaderReflection> reflect_spirv(const std::vector<uint32_t>& spirv, const ReflectionOptions& opt = {});
} // namespace vshadersystem