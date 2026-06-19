//! Realistic SPIR-V -> WGSL cook tests. Fixtures in tests/fixtures/*.spv are glslangValidator output
//! (--target-env vulkan1.1, spv1.3) for representative shader patterns that previously broke naga:
//! combined image samplers (single/cube/array), the bindless binding_array (must NOT be corrupted by the
//! combined-sampler splitter), storage images, and a realistic post-process shader. The cook must turn
//! all of them into valid WGSL.

fn convert_ok(name: &str, spv: &[u8]) -> String {
    match naga_wgsl::spirv_to_wgsl(spv) {
        Ok(wgsl) => {
            assert!(!wgsl.trim().is_empty(), "{name}: produced empty WGSL");
            wgsl
        }
        Err(e) => panic!("{name}: conversion failed: {e}"),
    }
}

#[test]
fn combined_sampler_2d() {
    let wgsl = convert_ok("combined_sampler", include_bytes!("fixtures/combined_sampler.spv"));
    // The combined sampler2D must have been split into a separate texture + sampler.
    assert!(wgsl.contains("texture_2d"), "expected texture_2d in WGSL:\n{wgsl}");
    assert!(wgsl.contains("sampler"), "expected sampler in WGSL");
}

#[test]
fn combined_sampler_cube() {
    let wgsl = convert_ok("combined_cube", include_bytes!("fixtures/combined_cube.spv"));
    assert!(wgsl.contains("texture_cube"), "expected texture_cube in WGSL:\n{wgsl}");
}

#[test]
fn combined_sampler_2d_array() {
    // sampler2DArray: a single combined sampler over an array texture (used widely: bloom, fxaa, ...).
    // (Array-OF-combined-samplers `sampler2D[N]` is NOT supported by the splitter, but the web shaders
    // never use it -- the only `sampler2D[]` is the Vulkan #else branch of the bindless shaders, which
    // the web cook does not see.)
    let wgsl = convert_ok("combined_array_dim", include_bytes!("fixtures/combined_array_dim.spv"));
    assert!(wgsl.contains("texture_2d_array"), "expected texture_2d_array in WGSL:\n{wgsl}");
}

#[test]
fn bindless_binding_array_is_preserved() {
    // Regression: the combined-sampler splitter must be SKIPPED here (no combined-sampler variable, only
    // a separate texture2D[] + sampler), leaving naga to emit a binding_array. Running the splitter here
    // previously corrupted it into a "handle address space" WGSL error.
    let wgsl = convert_ok("bindless_array", include_bytes!("fixtures/bindless_array.spv"));
    assert!(
        wgsl.contains("binding_array"),
        "bindless shader must keep its binding_array:\n{wgsl}"
    );
}

#[test]
fn storage_image_2d() {
    convert_ok("storage_image", include_bytes!("fixtures/storage_image.spv"));
}

#[test]
fn post_process_realistic() {
    convert_ok("post_realistic", include_bytes!("fixtures/post_realistic.spv"));
}
