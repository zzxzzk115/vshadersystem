includes("vshadersystem")

-- Android/WASM builds only the runtime library for now.
if not is_plat("android", "wasm") then
    includes("vshaderc")
end
