target("vshaderc")
	set_kind("binary")
	set_languages("cxx23")

	add_files("main.cpp")

	-- v1.0: vshaderc drives the Slang compile core (desktop-only). The legacy
	-- tool_cli path in the runtime library is retired in M0b.
	add_deps("vshaderc-lib")
	add_packages("slang-prebuilt")

	-- The Slang runtime dylibs use @rpath / $ORIGIN install names.
	if is_plat("macosx") then
		add_rpathdirs("@executable_path")
	elseif is_plat("linux") then
		add_rpathdirs("$ORIGIN")
	end

	-- Copy the Slang runtime (DLLs/.so/.dylib + standard module dir) next to the exe.
	after_build(function (target)
		local pkg = target:pkg("slang-prebuilt")
		if not pkg then return end
		local installdir = pkg:installdir()
		if not installdir then return end
		local copyAll = function (dir)
			if not os.isdir(dir) then return end
			for _, f in ipairs(os.files(path.join(dir, "*"))) do os.trycp(f, target:targetdir()) end
			for _, d in ipairs(os.dirs(path.join(dir, "*"))) do os.trycp(d, target:targetdir()) end
		end
		copyAll(path.join(installdir, "bin"))
		for _, so in ipairs(os.files(path.join(installdir, "lib", "*.so*"))) do os.trycp(so, target:targetdir()) end
		for _, dylib in ipairs(os.files(path.join(installdir, "lib", "*.dylib"))) do os.trycp(dylib, target:targetdir()) end
	end)

	-- set target directory
	set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshaderc")
