#include <vshadersystem/binary.hpp>
#include <vshadersystem/result.hpp>
#include <vshadersystem/system.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace vshadersystem;

static void print_usage()
{
    std::cout <<
        R"(vshaderc - offline shader compiler

Usage:
  vshaderc -i <input.vshader> -o <output.vshbin> -S <stage> [options]

Stages:
  vert, frag, comp, task, mesh, rgen, rmiss, rchit, rahit, rint

Options:
  -I <dir>         Add include directory (repeatable)
  -D <NAME=VALUE>  Define macro (repeatable; VALUE optional)
  --no-cache       Disable cache
  --cache <dir>    Cache directory (default: .vshader_cache)

Examples:
  vshaderc -i shaders/pbr.frag.vshader -o out/pbr.frag.vshbin -S frag -I shaders/include -D USE_FOO=1
)";
}

static bool parse_stage(const std::string& s, ShaderStage& out)
{
    if (s == "vert")
    {
        out = ShaderStage::eVert;
        return true;
    }
    if (s == "frag")
    {
        out = ShaderStage::eFrag;
        return true;
    }
    if (s == "comp")
    {
        out = ShaderStage::eComp;
        return true;
    }
    if (s == "task")
    {
        out = ShaderStage::eTask;
        return true;
    }
    if (s == "mesh")
    {
        out = ShaderStage::eMesh;
        return true;
    }
    if (s == "rgen")
    {
        out = ShaderStage::eRgen;
        return true;
    }
    if (s == "rmiss")
    {
        out = ShaderStage::eRmiss;
        return true;
    }
    if (s == "rchit")
    {
        out = ShaderStage::eRchit;
        return true;
    }
    if (s == "rahit")
    {
        out = ShaderStage::eRahit;
        return true;
    }
    if (s == "rint")
    {
        out = ShaderStage::eRint;
        return true;
    }
    return false;
}

static bool read_text_file(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    out.resize(size);
    f.read(out.data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

int main(int argc, char** argv)
{
    if (argc <= 1)
    {
        print_usage();
        return 1;
    }

    std::string              inPath;
    std::string              outPath;
    std::string              stageStr;
    std::vector<std::string> includeDirs;
    std::vector<Define>      defines;
    bool                     enableCache = true;
    std::string              cacheDir    = ".vshader_cache";

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];

        if (a == "-h" || a == "--help")
        {
            print_usage();
            return 0;
        }
        else if (a == "-i" && i + 1 < argc)
        {
            inPath = argv[++i];
        }
        else if (a == "-o" && i + 1 < argc)
        {
            outPath = argv[++i];
        }
        else if (a == "-S" && i + 1 < argc)
        {
            stageStr = argv[++i];
        }
        else if (a == "-I" && i + 1 < argc)
        {
            includeDirs.push_back(argv[++i]);
        }
        else if (a == "-D" && i + 1 < argc)
        {
            std::string def = argv[++i];
            auto        pos = def.find('=');
            Define      d;
            if (pos == std::string::npos)
            {
                d.name  = def;
                d.value = "";
            }
            else
            {
                d.name  = def.substr(0, pos);
                d.value = def.substr(pos + 1);
            }
            defines.push_back(std::move(d));
        }
        else if (a == "--no-cache")
        {
            enableCache = false;
        }
        else if (a == "--cache" && i + 1 < argc)
        {
            cacheDir = argv[++i];
        }
        else
        {
            std::cerr << "Unknown argument: " << a << "\n";
            print_usage();
            return 2;
        }
    }

    ShaderStage stage = ShaderStage::eFrag;
    if (!parse_stage(stageStr, stage))
    {
        std::cerr << "Invalid stage: " << stageStr << "\n";
        return 3;
    }

    if (inPath.empty() || outPath.empty())
    {
        std::cerr << "Input/output must be specified.\n";
        return 4;
    }

    std::string src;
    if (!read_text_file(inPath, src))
    {
        std::cerr << "Failed to read input file: " << inPath << "\n";
        return 5;
    }

    BuildRequest req;
    req.source.virtualPath  = inPath;
    req.source.sourceText   = std::move(src);
    req.options.stage       = stage;
    req.options.includeDirs = std::move(includeDirs);
    req.options.defines     = std::move(defines);
    req.enableCache         = enableCache;
    req.cacheDir            = cacheDir;

    auto r = build_shader(req);
    if (!r.isOk())
    {
        std::cerr << "Build failed: " << r.error().message << "\n";
        return 6;
    }

    auto w = write_vshbin_file(outPath, r.value().binary);
    if (!w.isOk())
    {
        std::cerr << "Write failed: " << w.error().message << "\n";
        return 7;
    }

    std::cout << "OK: wrote " << outPath << (r.value().fromCache ? " (cache)\n" : "\n");
    if (!r.value().log.empty())
        std::cout << r.value().log << "\n";

    return 0;
}
