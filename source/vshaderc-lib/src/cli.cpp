#include "vshaderc/cli.hpp"
#include "cook_cache.hpp"

#include "vshaderc/slang_build.hpp"
#include "vshaderc/slang_compiler.hpp"
#include "vshaderc/slang_metadata.hpp"
#include "vshaderc/slang_reflect.hpp"

#include "vshadersystem/engine_keywords.hpp"
#include "vshadersystem/hash.hpp"
#include "vshadersystem/shader_id.hpp"
#include "vshadersystem/variant_key.hpp"
#include "vshadersystem/vsh_format.hpp"

#include <cstdio>
#include <algorithm>
#include <charconv>
#include <memory>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace vshaderc::cli
{
    namespace fs = std::filesystem;
    using namespace vshadersystem;

    namespace
    {
        void err(const std::string& m) { std::fprintf(stderr, "error: %s\n", m.c_str()); }

        bool read_file(const std::string& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return false;
            std::ostringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        bool write_file(const std::string& path, const std::vector<uint8_t>& bytes)
        {
            return detail::atomic_write(path, bytes);
        }

        ShaderStage stage_from_name(const std::string& s)
        {
            if (s == "vert" || s == "vertex") return ShaderStage::eVert;
            if (s == "frag" || s == "fragment") return ShaderStage::eFrag;
            if (s == "geom" || s == "geometry") return ShaderStage::eGeom;
            if (s == "hull" || s == "tesc" || s == "tesscontrol") return ShaderStage::eHull;
            if (s == "domain" || s == "tese" || s == "tesseval") return ShaderStage::eDomain;
            if (s == "comp" || s == "compute") return ShaderStage::eComp;
            if (s == "task") return ShaderStage::eTask;
            if (s == "mesh") return ShaderStage::eMesh;
            if (s == "rgen") return ShaderStage::eRgen;
            if (s == "rmiss") return ShaderStage::eRmiss;
            if (s == "rchit") return ShaderStage::eRchit;
            if (s == "rahit") return ShaderStage::eRahit;
            if (s == "rint") return ShaderStage::eRint;
            return ShaderStage::eUnknown;
        }

        // Simple flag parser: pulls "-x value" / "--long value" pairs and repeatables.
        struct Args
        {
            std::vector<std::string> a;
            std::string              get(const std::string& key, const std::string& def = "") const
            {
                for (size_t i = 0; i + 1 < a.size(); ++i)
                    if (a[i] == key)
                        return a[i + 1];
                return def;
            }
            std::vector<std::string> getAll(const std::string& key) const
            {
                std::vector<std::string> out;
                for (size_t i = 0; i + 1 < a.size(); ++i)
                    if (a[i] == key)
                        out.push_back(a[i + 1]);
                return out;
            }
            bool has(const std::string& key) const
            {
                for (const auto& s : a)
                    if (s == key)
                        return true;
                return false;
            }
        };

        // Parse --matrix-layout <column|row> (default column-major, matching glm / GLSL / Vulkan).
        // Returns false on an invalid value.
        bool get_matrix_layout(const Args& args, MatrixLayout& out)
        {
            const std::string m = args.get("--matrix-layout");
            if (m.empty() || m == "column" || m == "col")
            {
                out = MatrixLayout::Column;
                return true;
            }
            if (m == "row")
            {
                out = MatrixLayout::Row;
                return true;
            }
            err("--matrix-layout must be 'column' or 'row'");
            return false;
        }

        // Resolve the per-shader default permutation values into a variantHash and the
        // matching macro defines, so `compile` emits the default variant directly.
        void default_keyword_defines(const ShaderMetadata& meta, SlangCompileOptions& co,
                                     VariantKey& key)
        {
            for (const auto& k : meta.keywords)
            {
                if (k.dispatch != KeywordDispatch::ePermutation)
                    continue;
                co.defines.push_back({k.name, std::to_string(k.defaultValue)});
                key.set(k.name, k.defaultValue);
            }
        }

        int cmd_compile(const Args& args)
        {
            const std::string in  = args.get("-i");
            const std::string out = args.get("-o");
            if (in.empty() || out.empty())
                return (err("compile requires -i <in.slang> -o <out.vshbin>"), 2);

            std::string source;
            if (!read_file(in, source))
                return (err("cannot read " + in), 1);

            const fs::path inPath(in);
            const std::string moduleName = inPath.stem().string();
            const std::string modulePath = inPath.filename().string();

            SlangCompileOptions co;
            co.emitWgsl = !args.has("--no-wgsl");
            co.debugInfo = args.has("--debug-info");
            co.optimize = args.has("--optimize");
            co.emitDxbc = args.has("--dxbc"); // Direct3D 12 SM5.1 (needs fxc; opt-in, Windows host)
            co.emitDxil = args.has("--dxil"); // Direct3D 12 SM6.0 (needs dxc; opt-in, Windows host)
            co.searchDirs.push_back(inPath.has_parent_path() ? inPath.parent_path().string() : ".");
            for (const auto& d : args.getAll("-I"))
                co.searchDirs.push_back(d);
            for (const auto& d : args.getAll("-D"))
            {
                auto eq = d.find('=');
                co.defines.push_back({d.substr(0, eq), eq == std::string::npos ? "1" : d.substr(eq + 1)});
            }
            if (!get_matrix_layout(args, co.matrixLayout))
                return 2;

            SlangCompiler compiler;
            if (!compiler.isValid())
                return (err("failed to initialize Slang"), 1);

            auto metaR = extract_shader_metadata(compiler, moduleName, modulePath, source, co);
            if (!metaR.isOk())
                return (err(metaR.error().message), 1);

            const std::string shaderId = args.get("--id", moduleName);

            VariantKey key;
            key.setShaderId(shaderId);
            default_keyword_defines(metaR.value(), co, key);

            auto cr = compiler.compileModule(moduleName, modulePath, source, co, &metaR.value());
            if (!cr.isOk())
                return (err(cr.error().message), 1);

            const ShaderStage wanted = stage_from_name(args.get("-S"));
            const SlangEntryPoint* pick = nullptr;
            for (const auto& ep : cr.value().entryPoints)
            {
                if (wanted == ShaderStage::eUnknown || ep.stage == wanted)
                {
                    pick = &ep;
                    break;
                }
            }
            if (!pick)
                return (err("no entry point matching stage in " + in), 1);

            ShaderBinary bin;
            bin.shaderIdHash   = shader_id_hash(shaderId);
            bin.stage          = pick->stage;
            bin.entryPointName = pick->name;
            bin.spirv          = pick->spirv;
            bin.wgsl           = pick->wgsl;
            bin.spirvHash      = pick->spirv.empty() ? 0 : xxhash64_words(pick->spirv);
            bin.reflection     = cr.value().reflection;
            bin.materialDesc   = cr.value().material;
            bin.keywords       = metaR.value().keywords;
            key.setStage(pick->stage);
            bin.variantHash = key.build();

            auto bytes = v1::write_binary(bin);
            if (!bytes.isOk() || !write_file(out, bytes.value()))
                return (err("failed to write " + out), 1);
            std::printf("compiled %s -> %s (stage=%s, spirv=%zu words, wgsl=%zu bytes)\n", in.c_str(),
                        out.c_str(), args.get("-S", "auto").c_str(), pick->spirv.size(), pick->wgsl.size());
            return 0;
        }

        int cmd_build(const Args& args)
        {
            const std::string root = args.get("--shader_root");
            const std::string out  = args.get("-o");
            if (root.empty() || out.empty())
                return (err("build requires --shader_root <dir> -o <out.vshlib>"), 2);

            EngineKeywordsFile        engineKw;
            std::vector<uint8_t>      vkwBytes;
            const std::string         vkwPath = args.get("--keywords-file");
            if (!vkwPath.empty())
            {
                std::string text;
                if (!read_file(vkwPath, text))
                    return (err("cannot read " + vkwPath), 1);
                auto kr = parse_engine_keywords_vkw(text);
                if (!kr.isOk())
                    return (err(kr.error().message), 1);
                engineKw  = kr.value();
                vkwBytes.assign(text.begin(), text.end());
            }

            std::vector<std::string> extraI = args.getAll("-I");

            MatrixLayout matrixLayout = MatrixLayout::Column;
            if (!get_matrix_layout(args, matrixLayout))
                return 2;


            uint32_t jobs = std::max(1u, std::min(4u, std::thread::hardware_concurrency()));
            if (args.has("--jobs"))
            {
                const auto value = args.get("--jobs");
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), jobs);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || jobs == 0 || jobs > 32)
                    return (err("--jobs must be between 1 and 32"), 2);
            }
            const bool quiet = args.has("--quiet");
            bool useCache = !args.has("--no-cache");
            std::string identity;
            if (useCache)
            {
                try { identity = detail::compiler_identity(args.has("--dxbc") || args.has("--dxil")); }
                catch (const std::exception& ex)
                {
                    err(std::string("cache disabled: ") + ex.what());
                    useCache = false;
                }
            }
            detail::CookCache cache(args.get("--cache-dir", out + ".cache"), identity);
            std::unique_ptr<SlangCompiler> compiler;
            std::vector<v1::LibraryEntry> entries;
            std::vector<FileDependency> allDependencies;
            if (!vkwPath.empty())
                allDependencies.push_back({fs::absolute(vkwPath).lexically_normal().generic_string(), true,
                                           xxhash64(vkwBytes.data(), vkwBytes.size())});
            std::vector<fs::path> files;
            for (const auto& de : fs::recursive_directory_iterator(root))
                if (de.is_regular_file() && de.path().extension() == ".slang")
                    files.push_back(de.path());
            std::sort(files.begin(), files.end());
            size_t compiled = 0, cached = 0;
            for (const auto& path : files)
            {
                std::string source;
                if (!read_file(path.string(), source))
                    return (err("cannot read " + path.string()), 1);
                const FileDependency mainFile{fs::absolute(path).lexically_normal().generic_string(), true,
                                              xxhash64(source)};
                allDependencies.push_back(mainFile);
                std::string rel = fs::relative(path, root).generic_string();
                rel.resize(rel.size() - 6);
                ShaderBuildOptions bo;
                bo.shaderId             = rel;
                bo.jobs                 = jobs;
                bo.compile.emitWgsl     = !args.has("--no-wgsl");
                bo.compile.debugInfo    = args.has("--debug-info");
                bo.compile.optimize     = args.has("--optimize");
                bo.compile.emitDxbc     = args.has("--dxbc");
                bo.compile.emitDxil     = args.has("--dxil");
                bo.compile.matrixLayout = matrixLayout;
                bo.compile.searchDirs.push_back(path.parent_path().string());
                bo.compile.searchDirs.push_back(root);
                for (const auto& dir : extraI)
                    bo.compile.searchDirs.push_back(dir);
                if (!vkwPath.empty())
                    bo.engineKeywords = &engineKw;
                const auto key = cache.key(path, source, bo, vkwBytes);
                std::vector<v1::LibraryEntry> shaderEntries;
                std::vector<FileDependency> dependencies;
                if (useCache && cache.load(key, shaderEntries, dependencies))
                {
                    ++cached;
                    if (!quiet)
                        std::printf("[vshaderc] cached %s: %zu stage variant(s)\n", rel.c_str(), shaderEntries.size());
                }
                else
                {
                    if (!compiler)
                        compiler = std::make_unique<SlangCompiler>();
                    if (!compiler->isValid())
                        return (err("failed to initialize Slang"), 1);
                    if (!quiet)
                    {
                        std::printf("[vshaderc] compiling %s\n", rel.c_str());
                        std::fflush(stdout);
                        bo.onVariant = [&rel](uint32_t current, uint32_t total,
                                              const std::vector<std::pair<std::string, uint32_t>>& values, bool skipped) {
                            std::printf("[vshaderc] %s: permutation %u/%u %s", rel.c_str(), current, total,
                                        skipped ? "skipped" : "compiling");
                            for (const auto& [name, value] : values)
                                std::printf(" %s=%u", name.c_str(), value);
                            std::printf("\n");
                            std::fflush(stdout);
                        };
                    }
                    auto br = build_shader(*compiler, path.stem().string(), path.filename().string(), source, bo);
                    if (!br.isOk())
                        return (err(rel + ": " + br.error().message), 1);
                    bool complete = true;
                    for (const auto& variant : br.value().variants)
                    {
                        if ((bo.compile.emitDxbc && variant.dxbc.empty()) || (bo.compile.emitDxil && variant.dxil.empty()))
                            complete = false; // best-effort target failures must be retried next build
                        auto bin = v1::write_binary(to_shader_binary(variant, br.value().shaderIdHash, br.value().keywords));
                        if (!bin.isOk())
                            return (err("serialize failed for " + rel), 1);
                        shaderEntries.push_back({variant.variantHash, variant.stage, std::move(bin.value())});
                    }
                    dependencies = std::move(br.value().fileDependencies);
                    dependencies.push_back(mainFile);
                    if (!cache.matches(dependencies, true))
                        return (err("shader inputs changed during compilation: " + rel), 1);
                    if (useCache && complete && !cache.save(key, dependencies, shaderEntries))
                        err("could not save shader cache for " + rel);
                    ++compiled;
                    if (!quiet)
                        std::printf("[vshaderc] built %s: %zu stage variant(s), %u permutation(s) skipped\n",
                                    rel.c_str(), shaderEntries.size(), br.value().skipped);
                }
                allDependencies.insert(allDependencies.end(), dependencies.begin(), dependencies.end());
                for (auto& entry : shaderEntries)
                    entries.push_back(std::move(entry));
            }
            if (!cache.matches(allDependencies, true))
                return (err("shader inputs changed during build; keeping previous library"), 1);
            std::vector<fs::path> currentFiles;
            for (const auto& de : fs::recursive_directory_iterator(root))
                if (de.is_regular_file() && de.path().extension() == ".slang")
                    currentFiles.push_back(de.path());
            std::sort(currentFiles.begin(), currentFiles.end());
            if (currentFiles != files)
                return (err("shader tree changed during build; keeping previous library"), 1);
            auto bytes = v1::write_library(entries, vkwBytes.empty() ? nullptr : &vkwBytes);
            if (!bytes.isOk() || !write_file(out, bytes.value()))
                return (err("failed to write " + out), 1);
            std::printf("built %zu shader(s), %zu variant(s) -> %s (compiled %zu, cached %zu)\n",
                        files.size(), entries.size(), out.c_str(), compiled, cached);
            return 0;
        }

        int cmd_pack_slang(const Args& args)
        {
            const std::string root = args.get("--root");
            const std::string out  = args.get("-o");
            if (root.empty() || out.empty())
                return (err("pack-slang requires --root <dir> -o <out.vshslang>"), 2);
            std::string ext = args.get("--ext", ".slang");

            std::vector<v1::SourceFile> files;
            for (const auto& de : fs::recursive_directory_iterator(root))
            {
                if (!de.is_regular_file() || de.path().extension() != ext)
                    continue;
                std::string text;
                if (!read_file(de.path().string(), text))
                    return (err("cannot read " + de.path().string()), 1);
                files.push_back({fs::relative(de.path(), root).generic_string(), std::move(text)});
            }
            auto bytes = v1::write_source_pack(files);
            if (!bytes.isOk() || !write_file(out, bytes.value()))
                return (err("failed to write " + out), 1);
            std::printf("packed %zu .slang file(s) -> %s\n", files.size(), out.c_str());
            return 0;
        }

        // Strip a .vshlib down to the bytecode needed for a set of targets, for release packaging.
        int cmd_strip(const Args& args)
        {
            const std::string in  = args.get("-i");
            const std::string out = args.get("-o");
            if (in.empty() || out.empty())
                return (err("strip requires -i <in.vshlib> -o <out.vshlib>"), 2);

            struct KeepSet
            {
                bool spirv = false, wgsl = false, dxbc = false, dxil = false;
            } keep;
            bool       any   = false;
            const auto apply = [&](const std::string& list, bool byApi) {
                size_t start = 0;
                while (start <= list.size())
                {
                    const size_t      comma = list.find(',', start);
                    const std::string tok =
                        list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                    if (!tok.empty())
                    {
                        any = true;
                        if (byApi)
                        {
                            if (tok == "vulkan" || tok == "opengl" || tok == "gl" || tok == "metal")
                                keep.spirv = true;
                            else if (tok == "webgpu" || tok == "wgpu")
                                keep.wgsl = true;
                            else if (tok == "d3d12" || tok == "dx12")
                                keep.dxbc = keep.dxil = true;
                            else
                                err("strip: unknown --api '" + tok + "' (ignored)");
                        }
                        else if (tok == "spirv")
                            keep.spirv = true;
                        else if (tok == "wgsl")
                            keep.wgsl = true;
                        else if (tok == "dxbc")
                            keep.dxbc = true;
                        else if (tok == "dxil")
                            keep.dxil = true;
                        else
                            err("strip: unknown --keep '" + tok + "' (ignored)");
                    }
                    if (comma == std::string::npos)
                        break;
                    start = comma + 1;
                }
            };
            apply(args.get("--api"), true);
            apply(args.get("--keep"), false);
            if (!any)
                return (err("strip needs --api <vulkan,webgpu,d3d12,...> or --keep <spirv,wgsl,dxbc,dxil>"), 2);

            std::string text;
            if (!read_file(in, text))
                return (err("cannot read " + in), 1);
            const std::vector<uint8_t> bytes(text.begin(), text.end());
            auto                       lib = v1::read_library(bytes);
            if (!lib.isOk())
                return (err(lib.error().message), 1);

            std::vector<v1::LibraryEntry> entries;
            for (const auto& e : lib.value().entries)
            {
                auto bin = v1::read_binary(e.blob);
                if (!bin.isOk())
                    return (err("strip: " + bin.error().message), 1);
                ShaderBinary b = bin.value();
                if (!keep.spirv)
                {
                    b.spirv.clear();
                    b.spirvHash = 0;
                }
                if (!keep.wgsl)
                    b.wgsl.clear();
                if (!keep.dxbc)
                    b.dxbc.clear();
                if (!keep.dxil)
                    b.dxil.clear();
                auto reblob = v1::write_binary(b);
                if (!reblob.isOk())
                    return (err("strip: serialize failed"), 1);
                entries.push_back({e.variantHash, e.stage, reblob.value()});
            }
            const std::vector<uint8_t>& vkw = lib.value().engineKeywords;
            auto libBytes = v1::write_library(entries, vkw.empty() ? nullptr : &vkw);
            if (!libBytes.isOk() || !write_file(out, libBytes.value()))
                return (err("failed to write " + out), 1);
            std::printf("stripped %zu variant(s) -> %s (kept:%s%s%s%s)\n", entries.size(), out.c_str(),
                        keep.spirv ? " spirv" : "", keep.wgsl ? " wgsl" : "", keep.dxbc ? " dxbc" : "",
                        keep.dxil ? " dxil" : "");
            return 0;
        }

        void usage()
        {
            std::printf(
                "vshaderc (v1.4, Slang)\n"
                "  compile -i <in.slang> -o <out.vshbin> [-S <stage>] [-I <dir>] [-D K=V] [--no-wgsl]\n"
                "          [--dxbc] [--dxil] [--matrix-layout column|row] [--id <id>]\n"
                "  build --shader_root <dir> -o <out.vshlib> [--keywords-file <vkw>] [-I <dir>] [--no-wgsl] [--quiet]\n"
                "        [--dxbc] [--dxil] [--matrix-layout column|row]\n"
                "  strip -i <in.vshlib> -o <out.vshlib> --api <list> | --keep <list>\n"
                "  pack-slang --root <dir> -o <out.vshslang> [--ext .slang]\n"
                "\n"
                "  --cache-dir      override per-shader cache directory (default: <output>.cache)\n"
                "  --no-cache       rebuild without reading/writing caches\n"
                "  --jobs <1..32>   parallel permutation workers (default: min(4, CPU threads))\n"
                "  --debug-info     embed shader source, line and function information (Release too)\n"
                "  --optimize       use Slang high optimization independently of --debug-info\n"
                "  --dxbc / --dxil  also emit Direct3D 12 bytecode (SM5.1 via fxc / SM6.0 via dxc; Windows host)\n"
                "  --matrix-layout  memory layout for matrix constants (default: column, matches glm/GLSL/Vulkan)\n"
                "  strip --api      vulkan|opengl|metal (spirv), webgpu (wgsl), d3d12 (dxbc+dxil); or --keep\n"
                "                   spirv,wgsl,dxbc,dxil - drops the other bytecode per variant for release\n");
        }
    } // namespace

    int run(int argc, char** argv)
    {
        if (argc < 2)
            return (usage(), 2);
        Args args;
        for (int i = 2; i < argc; ++i)
            args.a.emplace_back(argv[i]);

        const std::string cmd = argv[1];
        try
        {
            if (cmd == "compile")
                return cmd_compile(args);
            if (cmd == "build")
                return cmd_build(args);
            if (cmd == "pack-slang")
                return cmd_pack_slang(args);
            if (cmd == "strip")
                return cmd_strip(args);
            if (cmd == "-h" || cmd == "--help" || cmd == "help")
                return (usage(), 0);
            err("unknown command: " + cmd);
            usage();
            return 2;
        }
        catch (const std::exception& ex)
        {
            err(ex.what());
            return 1;
        }
    }
} // namespace vshaderc::cli
