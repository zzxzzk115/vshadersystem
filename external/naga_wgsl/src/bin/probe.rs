fn main() {
    let path = std::env::args().nth(1).expect("usage: probe <spv> [--split]");
    let split = std::env::args().any(|a| a == "--split");
    let bytes = std::fs::read(&path).expect("read spv");

    let mut words = spirv_webgpu_transform::u8_slice_to_u32_vec(&bytes);
    if split {
        let mut corrections: Option<spirv_webgpu_transform::CorrectionMap> = None;
        words = spirv_webgpu_transform::combimgsampsplitter(&words, &mut corrections)
            .expect("combimgsampsplitter failed");
        eprintln!("after combimgsampsplitter: {} words, corrections={:?}", words.len(), corrections);
    }
    let spv = spirv_webgpu_transform::u32_slice_to_u8_vec(&words);

    let opts = naga::front::spv::Options::default();
    let module = match naga::front::spv::parse_u8_slice(&spv, &opts) {
        Ok(m) => m,
        Err(e) => { eprintln!("SPV-IN ERROR: {:?}", e); std::process::exit(1); }
    };
    let info = match naga::valid::Validator::new(
        naga::valid::ValidationFlags::all(), naga::valid::Capabilities::all()).validate(&module) {
        Ok(i) => i,
        Err(e) => { eprintln!("VALIDATE ERROR: {:?}", e); std::process::exit(2); }
    };
    match naga::back::wgsl::write_string(&module, &info, naga::back::wgsl::WriterFlags::empty()) {
        Ok(wgsl) => { println!("{}", wgsl); eprintln!("OK"); }
        Err(e) => { eprintln!("WGSL ERROR: {:?}", e); std::process::exit(3); }
    }
}
