target("spirv-tools")
    set_kind("static")
    set_warnings("none")

    add_includedirs(
        "include",
        ".",
        "source",
        "../spirv-headers/include",
        "../spirv-headers/include/spirv/unified1",
        {public = true}
    )

    -- Match core + optimizer libraries used by Tint's SPIR-V reader path.
    add_files("source/*.cpp")
    add_files("source/util/*.cpp")
    add_files("source/val/*.cpp")
    add_files("source/opt/*.cpp")
    remove_files("source/mimalloc.cpp", "source/pch_source.cpp", "source/opt/pch_source_opt.cpp")
