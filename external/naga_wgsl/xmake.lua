-- naga-based SPIR-V -> WGSL converter, built as a Rust staticlib via cargo and linked into
-- vshadersystem (replaces Tint). Requires the Rust toolchain (cargo + rustup) to be installed.

-- Map the xmake target (plat, arch) to a Rust target triple so the cargo-built staticlib matches the
-- arch xmake links against. Without this, cargo builds for the host triple (e.g. x86_64) and the linker
-- rejects it on 32-bit (-m32) / arm64 targets ("skipping incompatible libnaga_wgsl.a").
local function naga_rust_triple()
    if is_plat("windows") then
        if is_arch("x64", "x86_64") then return "x86_64-pc-windows-msvc" end
        if is_arch("x86", "i386") then return "i686-pc-windows-msvc" end
        if is_arch("arm64", "aarch64") then return "aarch64-pc-windows-msvc" end
    elseif is_plat("linux") then
        if is_arch("x86_64", "x64") then return "x86_64-unknown-linux-gnu" end
        if is_arch("x86", "i386") then return "i686-unknown-linux-gnu" end
        if is_arch("arm64", "aarch64") then return "aarch64-unknown-linux-gnu" end
    elseif is_plat("macosx") then
        if is_arch("x86_64", "x64") then return "x86_64-apple-darwin" end
        if is_arch("arm64", "aarch64") then return "aarch64-apple-darwin" end
    end
    return nil
end

local naga_triple = naga_rust_triple()
local naga_outdir = naga_triple and ("target/" .. naga_triple .. "/release") or "target/release"

target("naga_wgsl")
    set_kind("phony")
    set_default(false) -- only built when something (vshadersystem on host) depends on it
    add_includedirs("include", {public = true})

    on_build(function (target)
        import("lib.detect.find_tool")
        local function find_bin(name)
            local t = find_tool(name)
            if t and t.program then return t.program end
            local home = os.getenv("USERPROFILE") or os.getenv("HOME")
            if home then
                local p = path.join(home, ".cargo", "bin", is_host("windows") and (name .. ".exe") or name)
                if os.isfile(p) then return p end
            end
            return nil
        end

        local cargobin = find_bin("cargo")
        if not cargobin then
            raise("cargo (Rust toolchain) not found - install Rust to build naga_wgsl")
        end

        local triple = naga_triple
        if triple then
            -- Ensure the target's std is installed (no-op if already present), then build for it.
            local rustupbin = find_bin("rustup")
            if rustupbin then
                os.vrunv(rustupbin, {"target", "add", triple})
            end
            -- --lib: build ONLY the staticlib that vshaderc links. Building the probe bin (or tests)
            -- would require linking an executable, which fails under cross-compile (e.g. linux arm64) where
            -- only the host linker is available -> "incompatible with elf64-x86-64". The staticlib is just
            -- archived, so it cross-builds fine.
            os.vrunv(cargobin, {"build", "--release", "--lib", "--target", triple}, {curdir = os.scriptdir()})
        else
            os.vrunv(cargobin, {"build", "--release", "--lib"}, {curdir = os.scriptdir()})
        end
    end)

    -- Expose the cargo-produced staticlib + the system libs the Rust std needs, per platform.
    add_linkdirs(path.join(os.scriptdir(), naga_outdir), {public = true})
    add_links("naga_wgsl", {public = true})
    if is_plat("windows") then
        add_syslinks("ws2_32", "userenv", "ntdll", "bcrypt", "advapi32", "kernel32", "user32",
                     "ole32", "oleaut32", {public = true})
    elseif is_plat("linux") then
        add_syslinks("pthread", "dl", "m", {public = true})
    elseif is_plat("macosx") then
        add_syslinks("pthread", "dl", {public = true})
        add_frameworks("CoreFoundation", "Security", {public = true})
    end
target_end()
