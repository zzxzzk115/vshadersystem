-- set project name
set_project("vshadersystem")

-- set project version
set_version("1.0.1")

-- set language version: C++ 23
set_languages("cxx23")

-- root ?
local is_root = (os.projectdir() == os.scriptdir())
set_config("vshadersystem_root", is_root)
set_config("vshadersystem_project_dir", os.scriptdir())

-- global options
option("vshadersystem_build_examples") -- build examples?
    set_default(true)
    set_showmenu(true)
    set_description("Enable vshadersystem examples")
option_end()

-- Build the host/offline compiler (vshaderc + tools), which links the prebuilt Slang
-- compiler. Desktop-only: the runtime library never depends on Slang, so Android/WASM
-- leave this off and only build the loader.
option("vshadersystem_build_compiler")
    set_default(not (is_plat("android", "wasm")))
    set_showmenu(true)
    set_description("Enable the Slang-based offline compiler (vshaderc, tools)")
option_end()

-- Build the test suite (needs the Slang compiler; desktop only).
option("vshadersystem_build_tests")
    set_default(not (is_plat("android", "wasm")))
    set_showmenu(true)
    set_description("Enable the vshadersystem test suite")
option_end()

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/Zc:preprocessor", {tools = {"msvc", "cl"}}) -- enable conforming preprocessor (__VA_OPT__)
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")
    local runtime = get_config("runtimes")
    if runtime then
        set_runtimes(runtime)
    elseif is_mode("debug") then
        runtime = "MTd"
        set_runtimes(runtime)
    else
        runtime = "MT"
        set_runtimes(runtime)
    end
else
    add_cxxflags("-fexceptions")
end

-- add rules
rule("clangd.config")
    on_config(function (target)
        if is_host("windows") then
            os.cp(".clangd.win", ".clangd")
        else
            os.cp(".clangd.nowin", ".clangd")
        end
    end)
rule_end()

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})
add_rules("clangd.config")

-- add repositories
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")
-- local package overrides (prebuilt Slang 2026.11 for the offline compiler; see the package)
add_repositories("vshadersystem-local-repo xmake/xmake-repo")

-- The offline compiler links a prebuilt Slang (2026.11) that drives the compiler
-- in-process. Host/desktop only. NOTE: an option's set_default cannot see the target
-- platform reliably (it is evaluated before -p is applied), so the platform gate must
-- use is_plat() HERE, at include time, where the platform is known. Android/WASM build
-- the loader only and never pull in Slang.
local build_compiler = has_config("vshadersystem_build_compiler") and not is_plat("android", "wasm")

if build_compiler then
    add_requires("slang-prebuilt 2026.11")
end

-- include source
includes("source")

-- host/offline compiler tools
if build_compiler then
    includes("tools")
end

-- if build examples, then include examples
if has_config("vshadersystem_build_examples") then
    includes("examples")
end

-- test suite (Slang pipeline + runtime format); desktop only
if has_config("vshadersystem_build_tests") and build_compiler then
    add_requires("doctest")
    includes("tests")
end
