target("example_keywords")
    set_kind("phony")

    on_run(function (target)
        print("Building shaders...")
        os.exec("xmake run vshaderc build --shader_root $(scriptdir)/shaders --keywords-file $(scriptdir)/shaders/builtin_keywords.vkw -o $(scriptdir)/out/shaders.vshlib --verbose")
    end)
