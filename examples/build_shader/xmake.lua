target("example_build_shader")
	set_kind("binary")

	add_files("src/main.cpp")

	add_deps("vshadersystem")

	-- copy scriptdir/shaders to targetdir/shaders
	before_build(function (target)
		os.cp("$(scriptdir)/shaders", path.join(target:targetdir(), "shaders"))
	end)

	-- set target directory
	set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshadersystem/examples/example_build_shader")