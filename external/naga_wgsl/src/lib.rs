//! Thin C ABI wrapper around naga's SPIR-V -> WGSL conversion.
//!
//! vshadersystem's cook previously used Tint for this step, but Tint's SPIR-V reader cannot emit
//! `binding_array` (it produces an `array<texture>` in the handle address space, which is invalid
//! WGSL). naga's SPIR-V frontend + WGSL backend handle bindless texture arrays correctly, which is
//! required for the unified deferred renderer (bindless on Vulkan and native WebGPU/wgpu).

use std::ffi::CString;
use std::os::raw::c_char;

/// Convert a SPIR-V module (as 32-bit words) to WGSL text.
///
/// On success returns 0 and writes a newly-allocated, NUL-terminated UTF-8 WGSL string to `*out`.
/// On failure returns a non-zero code and writes a NUL-terminated error message to `*out`.
/// The caller must release the returned string with `naga_wgsl_string_free`.
#[no_mangle]
pub extern "C" fn naga_wgsl_from_spirv(
    spv_words: *const u32,
    spv_word_count: usize,
    out: *mut *mut c_char,
) -> i32 {
    if out.is_null() {
        return -1;
    }
    if spv_words.is_null() || spv_word_count == 0 {
        write_out(out, "naga_wgsl: empty SPIR-V input");
        return -1;
    }

    let words = unsafe { std::slice::from_raw_parts(spv_words, spv_word_count) };
    let mut bytes = Vec::with_capacity(words.len() * 4);
    for w in words {
        bytes.extend_from_slice(&w.to_le_bytes());
    }

    match convert(&bytes) {
        Ok(wgsl) => {
            write_out(out, &wgsl);
            0
        }
        Err(err) => {
            write_out(out, &err);
            1
        }
    }
}

/// Free a string previously returned by `naga_wgsl_from_spirv`.
#[no_mangle]
pub extern "C" fn naga_wgsl_string_free(s: *mut c_char) {
    if !s.is_null() {
        unsafe {
            let _ = CString::from_raw(s);
        }
    }
}

fn write_out(out: *mut *mut c_char, s: &str) {
    let c = CString::new(s).unwrap_or_else(|_| CString::new("naga_wgsl: message contained NUL").unwrap());
    unsafe {
        *out = c.into_raw();
    }
}

fn convert(spirv_bytes: &[u8]) -> Result<String, String> {
    // naga's SPIR-V reader cannot read glslang's combined image samplers: it only builds sampled images
    // from OpSampledImage (separate texture + sampler), whereas glslang emits an OpLoad of an
    // OpTypeSampledImage variable and samples that directly -> InvalidId. Split combined samplers into
    // separate texture+sampler first. The splitter appends the sampler at binding+1, which matches how
    // the RHI splits a combined-image-sampler descriptor (texture b, sampler b+1), so no reflection or
    // bind-group changes are needed. This is a no-op for shaders that already use separate samplers.
    let words = spirv_webgpu_transform::u8_slice_to_u32_vec(spirv_bytes);
    let mut corrections: Option<spirv_webgpu_transform::CorrectionMap> = None;
    let words = spirv_webgpu_transform::combimgsampsplitter(&words, &mut corrections)
        .map_err(|_| "spirv-webgpu-transform: combimgsampsplitter failed".to_string())?;
    let spirv_bytes = spirv_webgpu_transform::u32_slice_to_u8_vec(&words);

    let options = naga::front::spv::Options::default();
    let module = naga::front::spv::parse_u8_slice(&spirv_bytes, &options)
        .map_err(|e| format!("naga spv-in: {e:?}"))?;

    // Validate with all capabilities: the cook only produces WGSL text; the runtime validates it
    // against the actual device features (binding arrays, etc.).
    let info = naga::valid::Validator::new(
        naga::valid::ValidationFlags::all(),
        naga::valid::Capabilities::all(),
    )
    .validate(&module)
    .map_err(|e| format!("naga validate: {e:?}"))?;

    naga::back::wgsl::write_string(&module, &info, naga::back::wgsl::WriterFlags::empty())
        .map_err(|e| format!("naga wgsl-out: {e:?}"))
}
