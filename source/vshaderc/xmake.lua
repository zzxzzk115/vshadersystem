target("vshaderc")
	set_kind("binary")

	add_files("main.cpp")

	add_deps("vshadersystem")

	-- set target directory
	set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshaderc")