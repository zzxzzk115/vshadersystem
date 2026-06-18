#pragma once

// C ABI for the naga-based SPIR-V -> WGSL converter (see external/naga_wgsl/src/lib.rs).
// Replaces Tint, whose SPIR-V reader cannot emit `binding_array` for bindless texture arrays.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Convert a SPIR-V module (32-bit words) to WGSL text.
    // Returns 0 on success and writes a newly-allocated, NUL-terminated UTF-8 WGSL string to *out.
    // Returns non-zero on failure and writes a NUL-terminated error message to *out.
    // The returned string must be released with naga_wgsl_string_free.
    int32_t naga_wgsl_from_spirv(const uint32_t* spv_words, size_t spv_word_count, char** out);

    // Release a string returned by naga_wgsl_from_spirv.
    void naga_wgsl_string_free(char* s);

#ifdef __cplusplus
}
#endif
