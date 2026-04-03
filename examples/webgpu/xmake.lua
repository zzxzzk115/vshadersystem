target("example_webgpu")
    set_kind("binary")

    add_files("src/main.cpp")
    add_deps("vshadersystem")

    if is_plat("wasm") then
        -- Web-side shader compile + SPIR-V/WGSL conversion has high peak memory.
        -- Use a larger heap budget and allow growth to avoid runtime OOM.
        add_ldflags(
            "-sWASM=1",
            "-sASYNCIFY",
            "-sGL_ENABLE_GET_PROC_ADDRESS=1",
            "-sASSERTIONS=1",
            "-sALLOW_MEMORY_GROWTH=1",
            "-sINITIAL_MEMORY=134217728",
            "-sMAXIMUM_MEMORY=1073741824",
            {force = true}
        )
    end

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshadersystem/examples/example_webgpu")
