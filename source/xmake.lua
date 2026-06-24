includes("vshadersystem")

-- Host/offline Slang compile core + CLI (links prebuilt Slang). Desktop-only; gated on
-- the compiler option AND is_plat (option defaults can't see the platform), so Android/
-- WASM build only the loader.
if has_config("vshadersystem_build_compiler") and not is_plat("android", "wasm") then
    includes("vshaderc-lib")
    includes("vshaderc")
end
