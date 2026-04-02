target("example_webgpu")
    set_kind("binary")

    add_files("src/main.cpp")
    add_deps("vshadersystem")

    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vshadersystem/examples/example_webgpu")
