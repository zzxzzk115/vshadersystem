-- naga-based SPIR-V -> WGSL converter, built as a Rust staticlib via cargo and linked into
-- vshadersystem (replaces Tint). Requires the Rust toolchain (cargo) to be installed.
target("naga_wgsl")
    set_kind("phony")
    set_default(false) -- only built when something (vshadersystem on host) depends on it
    add_includedirs("include", {public = true})

    on_build(function (target)
        import("lib.detect.find_tool")
        local cargo = find_tool("cargo")
        local cargobin = cargo and cargo.program
        if not cargobin then
            -- rustup default install location when not on PATH
            local home = os.getenv("USERPROFILE") or os.getenv("HOME")
            if home then
                local p = path.join(home, ".cargo", "bin", is_host("windows") and "cargo.exe" or "cargo")
                if os.isfile(p) then cargobin = p end
            end
        end
        if not cargobin then
            raise("cargo (Rust toolchain) not found - install Rust to build naga_wgsl")
        end
        os.vrunv(cargobin, {"build", "--release"}, {curdir = os.scriptdir()})
    end)

    -- Expose the cargo-produced staticlib + the Windows system libs the Rust std needs.
    add_linkdirs(path.join(os.scriptdir(), "target", "release"), {public = true})
    add_links("naga_wgsl", {public = true})
    if is_plat("windows") then
        add_syslinks("ws2_32", "userenv", "ntdll", "bcrypt", "advapi32", "kernel32", "user32",
                     "ole32", "oleaut32", {public = true})
    end
target_end()
