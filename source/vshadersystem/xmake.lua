-- note: spirv-cross & glslang must require the same vulkan sdk version
-- we use 1.4.335
local dep_configs = {debug = is_mode("debug")}
if is_plat("windows") then
    dep_configs.runtimes = get_config("runtimes") or (is_mode("debug") and "MDd" or "MD")
end
add_requires("spirv-cross vulkan-sdk-1.4.335", {configs = dep_configs, system = false})
add_requires("glslang 1.4.335+0", {configs = dep_configs, system = false})
add_requires("xxhash")

target("vshadersystem")
	set_kind("static")

	add_headerfiles("include/(vshadersystem/**.hpp)")
	add_includedirs("include", {public = true})

	add_files("src/**.cpp")

	add_packages("glslang", "spirv-cross", "xxhash", {public = true})

	add_deps("tint")
	add_defines("VSHADERSYSTEM_ENABLE_TINT=1", {public = true})

	-- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshadersystem")
