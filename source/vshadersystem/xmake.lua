-- The runtime library only loads compiled binaries: it has NO Slang/glslang/spirv-cross
-- dependency and builds on every platform (incl. Android/WASM). Hashing uses xxhash.
add_requires("xxhash")

target("vshadersystem")
	set_kind("static")

	add_headerfiles("include/(vshadersystem/**.hpp)")
	add_includedirs("include", {public = true})

	add_files("src/**.cpp")

	add_packages("xxhash", {public = true})

	-- set target directory
	set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshadersystem")
