-- note: spirv-cross & glslang must require the same vulkan sdk version
-- we use 1.4.335
local dep_configs = {debug = is_mode("debug")}
if is_plat("windows") then
    dep_configs.runtimes = get_config("runtimes") or (is_mode("debug") and "MTd" or "MT")
end
add_requires("spirv-cross vulkan-sdk-1.4.335", {configs = dep_configs, system = false})
add_requires("glslang 1.4.335+0", {configs = dep_configs, system = false})
add_requires("xxhash")

target("vshadersystem")
	set_kind("static")

	add_headerfiles("include/(vshadersystem/**.hpp)")
	add_headerfiles("include/(vshadersystem/**.h)")
	add_includedirs("include", {public = true})

	add_files("src/**.cpp")

	add_packages("glslang", "spirv-cross", "xxhash", {public = true})

	-- naga (SPIR-V -> WGSL) is a host-only cook dependency built via cargo. Android/WASM build only the
	-- runtime library (no cooking), so they skip naga and the Rust toolchain entirely (wgsl.cpp compiles
	-- a stub there). See source/vshadersystem/src/wgsl.cpp.
	if not is_plat("android", "wasm") then
		add_deps("naga_wgsl")
		add_defines("VSHADERSYSTEM_ENABLE_NAGA=1")
	end

	-- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshadersystem")
