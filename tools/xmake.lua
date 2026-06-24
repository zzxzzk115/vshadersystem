-- Host/offline compiler tools. Desktop-only; these link the prebuilt Slang compiler.
-- Gated behind vshadersystem_build_compiler (default off, off on Android/WASM) so the
-- runtime library never pulls in Slang.

target("slang_spike")
    set_kind("binary")
    set_languages("cxx23")
    set_default(false)

    add_packages("slang-prebuilt")

    add_files("slang_spike/main.cpp")

    -- The Slang runtime dylibs use @rpath / $ORIGIN install names; let the exe find the
    -- copies placed next to it (see after_build).
    if is_plat("macosx") then
        add_rpathdirs("@executable_path")
    elseif is_plat("linux") then
        add_rpathdirs("$ORIGIN")
    end

    -- Copy the Slang runtime (DLLs/.so/.dylib + standard module dir) next to the exe so
    -- it runs without the package on PATH.
    after_build(function (target)
        local pkg = target:pkg("slang-prebuilt")
        if not pkg then
            return
        end
        local installdir = pkg:installdir()
        if not installdir then
            return
        end
        local copyAll = function (dir)
            if not os.isdir(dir) then return end
            for _, f in ipairs(os.files(path.join(dir, "*"))) do os.trycp(f, target:targetdir()) end
            for _, d in ipairs(os.dirs(path.join(dir, "*"))) do os.trycp(d, target:targetdir()) end
        end
        copyAll(path.join(installdir, "bin"))
        for _, so in ipairs(os.files(path.join(installdir, "lib", "*.so*"))) do os.trycp(so, target:targetdir()) end
        for _, dylib in ipairs(os.files(path.join(installdir, "lib", "*.dylib"))) do os.trycp(dylib, target:targetdir()) end
    end)

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/tools")
target_end()
