-- vshaderc-lib: host/offline Slang compile core. Desktop-only; links the prebuilt
-- Slang compiler. The runtime `vshadersystem` library never links this.
target("vshaderc-lib")
    set_kind("static")
    set_languages("cxx23")

    add_headerfiles("include/(vshaderc/**.hpp)")
    add_includedirs("include", {public = true})

    add_files("src/**.cpp")

    -- types.hpp / result.hpp come from the runtime library's public headers.
    add_deps("vshadersystem")
    add_packages("slang-prebuilt", {public = true})

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshaderc-lib")
target_end()
