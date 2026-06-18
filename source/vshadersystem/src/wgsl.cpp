#include "vshadersystem/wgsl.hpp"

#include <string>

// SPIR-V -> WGSL via naga (see external/naga_wgsl). Tint was replaced because its SPIR-V reader cannot
// emit `binding_array` for bindless texture arrays, which the unified deferred renderer needs.
//
// naga is a host-only cook dependency: it is only enabled on desktop host platforms (where shaders are
// cooked). Android/WASM build only the runtime library and never convert SPIR-V -> WGSL, so they compile
// a stub and avoid the Rust toolchain entirely.
#if defined(VSHADERSYSTEM_ENABLE_NAGA)
#include "naga_wgsl.h"
#endif

namespace vshadersystem
{
    Result<std::string> spirv_to_wgsl(const std::vector<uint32_t>& spirv)
    {
        if (spirv.empty())
            return Result<std::string>::err({ErrorCode::eInvalidArgument, "SPIR-V input is empty."});

#if defined(VSHADERSYSTEM_ENABLE_NAGA)
        char*         out  = nullptr;
        const int32_t rc   = naga_wgsl_from_spirv(spirv.data(), spirv.size(), &out);
        std::string   text = (out != nullptr) ? std::string(out) : std::string();
        naga_wgsl_string_free(out);

        if (rc != 0)
            return Result<std::string>::err(
                {ErrorCode::eCompileError, text.empty() ? "naga SPIR-V -> WGSL conversion failed." : text});

        return Result<std::string>::ok(std::move(text));
#else
        return Result<std::string>::err(
            {ErrorCode::eCompileError, "SPIR-V -> WGSL conversion is unavailable in this build (cook is host-only)."});
#endif
    }
} // namespace vshadersystem
