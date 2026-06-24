includes("vshadersystem")

-- Host/offline Slang compile core (links prebuilt Slang). Desktop-only.
if has_config("vshadersystem_build_compiler") then
    includes("vshaderc-lib")
end

-- Android/WASM builds only the runtime library for now.
if not is_plat("android", "wasm") then
    includes("vshaderc")
end
