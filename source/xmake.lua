includes("vshadersystem")

-- Android builds only the runtime library for now.
if not is_plat("android") then
    includes("vshaderc")
end
