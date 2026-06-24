target("example_module_reuse")
    set_kind("binary")
    set_languages("cxx23")
    add_files("main.cpp")

    add_deps("vshaderc-lib")
    add_packages("slang-prebuilt")

    if is_plat("macosx") then
        add_rpathdirs("@executable_path")
    elseif is_plat("linux") then
        add_rpathdirs("$ORIGIN")
    end

    -- copy the Slang runtime next to the exe
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

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshadersystem/examples/example_module_reuse")
