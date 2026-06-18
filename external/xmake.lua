-- naga (SPIR-V -> WGSL) is a host-only cook dependency. Android/WASM build only the runtime library and
-- don't cook shaders, so they skip it (and the Rust toolchain).
if not is_plat("android", "wasm") then
    includes("naga_wgsl")
end
