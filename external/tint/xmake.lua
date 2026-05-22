local abseil_configs = {}
if is_plat("windows") then
    abseil_configs.runtimes = get_config("runtimes") or (is_mode("debug") and "MDd" or "MD")
end
add_requires("abseil", {configs = abseil_configs, system = false})

target("tint")
    set_kind("static")
    set_warnings("none")
    set_languages("cxx20")

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
        add_defines(
            "TINT_BUILD_IS_LINUX=0",
            "TINT_BUILD_IS_MAC=0",
            "TINT_BUILD_IS_WIN=1",
            "NOMINMAX",
            "WIN32_LEAN_AND_MEAN=1"
        )
        add_cxxflags("/Zc:preprocessor", {tools = {"msvc", "cl"}})
    else
        add_defines("TINT_BUILD_IS_LINUX=0", "TINT_BUILD_IS_MAC=0", "TINT_BUILD_IS_WIN=0")
    end

    local files = os.files("src/tint/**/*.cc")
    local kept = {}
    local excluded_path_patterns = {
        "/cmd/",
        "/lang/hlsl/",
        "/lang/msl/",
        "/lang/glsl/",
        "/lang/null/",
        "/lang/spirv/writer/",
        "/lang/wgsl/reader/",
        "/lang/wgsl/inspector/",
        "/lang/wgsl/ls/",
        "/lang/wgsl/ast/transform/",
        "/lang/core/ir/binary/",
        "/utils/protos/"
    }
    local function match_any(haystack, patterns)
        for _, p in ipairs(patterns) do
            if haystack:find(p, 1, true) then
                return true
            end
        end
        return false
    end

    for _, f in ipairs(files) do
        local nf = f:gsub("\\", "/")

        if (not is_plat("windows")) and nf:endswith("tmpfile_windows.cc") then
            goto continue
        end
        if is_plat("windows") and nf:endswith("tmpfile_posix.cc") then
            goto continue
        end
        if is_plat("windows") and nf:endswith("_posix.cc") then
            goto continue
        end
        if (not is_plat("windows")) and nf:endswith("_windows.cc") then
            goto continue
        end
        if (not is_plat("linux")) and nf:endswith("_linux.cc") then
            goto continue
        end
        if (not is_plat("macosx")) and nf:endswith("_mac.cc") then
            goto continue
        end

        if nf:endswith("_test.cc")
            or nf:endswith("_bench.cc")
            or nf:endswith("_fuzz.cc")
            or match_any(nf, excluded_path_patterns) then
            goto continue
        end

        table.insert(kept, f)
        ::continue::
    end

    add_files(kept)
