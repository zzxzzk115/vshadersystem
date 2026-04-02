add_requires("abseil", {system = false})

target("tint")
    set_kind("static")
    set_warnings("none")

    add_deps("spirv-tools")

    add_includedirs("include", ".", {public = true})
    add_includedirs("../spirv-tools", "../spirv-tools/source", {public = false})

    add_packages("abseil", {public = true})

    add_defines(
        "TINT_BUILD_SPV_READER=1",
        "TINT_BUILD_WGSL_WRITER=1",
        "TINT_BUILD_WGSL_READER=0",
        "TINT_BUILD_SPV_WRITER=0",
        "TINT_BUILD_GLSL_WRITER=0",
        "TINT_BUILD_HLSL_WRITER=0",
        "TINT_BUILD_MSL_WRITER=0",
        "TINT_BUILD_NULL_WRITER=0",
        "TINT_BUILD_IR_BINARY=0",
        "TINT_BUILD_TINTD=0"
    )

    if is_plat("linux") then
        add_defines("TINT_BUILD_IS_LINUX=1", "TINT_BUILD_IS_MAC=0", "TINT_BUILD_IS_WIN=0")
    elseif is_plat("macosx") then
        add_defines("TINT_BUILD_IS_LINUX=0", "TINT_BUILD_IS_MAC=1", "TINT_BUILD_IS_WIN=0")
    elseif is_plat("windows") then
        add_defines("TINT_BUILD_IS_LINUX=0", "TINT_BUILD_IS_MAC=0", "TINT_BUILD_IS_WIN=1")
    else
        add_defines("TINT_BUILD_IS_LINUX=0", "TINT_BUILD_IS_MAC=0", "TINT_BUILD_IS_WIN=0")
    end

    local files = os.files("src/tint/**/*.cc")
    local kept = {}
    for _, f in ipairs(files) do
        if (not is_plat("windows")) and f:endswith("tmpfile_windows.cc") then
            goto continue
        end
        if is_plat("windows") and f:endswith("tmpfile_posix.cc") then
            goto continue
        end
        if (not is_plat("windows")) and f:endswith("_windows.cc") then
            goto continue
        end
        if (not is_plat("linux")) and f:endswith("_linux.cc") then
            goto continue
        end
        if (not is_plat("macosx")) and f:endswith("_mac.cc") then
            goto continue
        end

        if not f:endswith("_test.cc")
            and not f:endswith("_bench.cc")
            and not f:endswith("_fuzz.cc")
            and not f:find("/cmd/", 1, true)
            and not f:find("/lang/hlsl/", 1, true)
            and not f:find("/lang/msl/", 1, true)
            and not f:find("/lang/glsl/", 1, true)
            and not f:find("/lang/null/", 1, true)
            and not f:find("/lang/spirv/writer/", 1, true)
            and not f:find("/lang/wgsl/reader/", 1, true)
            and not f:find("/lang/wgsl/ls/", 1, true)
            and not f:find("/lang/wgsl/ast/transform/", 1, true)
            and not f:find("/lang/core/ir/binary/", 1, true) then
            table.insert(kept, f)
        end
        ::continue::
    end

    add_files(kept)
