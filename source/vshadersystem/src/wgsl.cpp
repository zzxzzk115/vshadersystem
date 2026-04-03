#include "vshadersystem/wgsl.hpp"

#include <tint/tint.h>

namespace vshadersystem
{
    namespace
    {
        inline void ensure_tint_initialized()
        {
            static const bool kTintInitialized = []() {
                tint::Initialize();
                return true;
            }();
            (void)kTintInitialized;
        }
    } // namespace

    Result<std::string> spirv_to_wgsl(const std::vector<uint32_t>& spirv)
    {
        if (spirv.empty())
            return Result<std::string>::err({ErrorCode::eInvalidArgument, "SPIR-V input is empty."});

        ensure_tint_initialized();

        auto wgsl = tint::SpirvToWgsl(spirv);
        if (wgsl != tint::Success)
            return Result<std::string>::err({ErrorCode::eCompileError, wgsl.Failure().reason});

        return Result<std::string>::ok(wgsl.Get());
    }
} // namespace vshadersystem
