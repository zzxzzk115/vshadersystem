-- Examples build variants with the Slang compile core, so they are desktop-only and
-- gated on the compiler option (Android/WASM build only the runtime loader).
if has_config("vshadersystem_build_compiler") then
    includes("runtime_load_library")
end
