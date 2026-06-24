includes("vshadersystem")

-- Host/offline Slang compile core + CLI (links prebuilt Slang). Desktop-only;
-- gated on the compiler option (off on Android/WASM, which build only the loader).
if has_config("vshadersystem_build_compiler") then
    includes("vshaderc-lib")
    includes("vshaderc")
end
