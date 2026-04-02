#include "vshadersystem/wgsl.hpp"

#include <tint/tint.h>

#include <mutex>

namespace vshadersystem
{
    Result<std::string> spirv_to_wgsl(const std::vector<uint32_t>& spirv)
    {
        if (spirv.empty())
            return Result<std::string>::err({ErrorCode::eInvalidArgument, "SPIR-V input is empty."});

        static std::once_flag tintInitOnce;
        std::call_once(tintInitOnce, []() { tint::Initialize(); });

        auto wgsl = tint::SpirvToWgsl(spirv);
        if (wgsl != tint::Success)
            return Result<std::string>::err({ErrorCode::eCompileError, wgsl.Failure().reason});

        return Result<std::string>::ok(wgsl.Get());
    }
} // namespace vshadersystem
