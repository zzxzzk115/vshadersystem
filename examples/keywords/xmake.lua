target("example_keywords")
    set_kind("phony")

    on_run(function (target)
        print("Cooking shaders...")
        os.exec("xmake run vshaderc cook -m $(scriptdir)/shader_cook.vcook -o $(scriptdir)/out/shaders.vshlib --jobs 8 --verbose")
    end)
