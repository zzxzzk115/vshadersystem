#include "vshaderc/cli.hpp"

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
            fs::path p(path);
            if (p.has_parent_path())
            {
                std::error_code ec;
                fs::create_directories(p.parent_path(), ec);
            }
            std::ofstream f(path, std::ios::binary);
            if (!f)
                return false;
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(f);
        }

        ShaderStage stage_from_name(const std::string& s)
        {
            if (s == "vert" || s == "vertex") return ShaderStage::eVert;
            if (s == "frag" || s == "fragment") return ShaderStage::eFrag;
            if (s == "geom" || s == "geometry") return ShaderStage::eGeom;
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
            co.searchDirs.push_back(inPath.has_parent_path() ? inPath.parent_path().string() : ".");
            for (const auto& d : args.getAll("-I"))
                co.searchDirs.push_back(d);
            for (const auto& d : args.getAll("-D"))
            {
                auto eq = d.find('=');
                co.defines.push_back({d.substr(0, eq), eq == std::string::npos ? "1" : d.substr(eq + 1)});
            }

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

            auto cr = compiler.compileModule(moduleName, modulePath, source, co);
            if (!cr.isOk())
                return (err(cr.error().message), 1);
            auto rr = reflect_shader(compiler, moduleName, modulePath, source, co, metaR.value());
            if (!rr.isOk())
                return (err(rr.error().message), 1);

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
            bin.reflection     = rr.value().reflection;
            bin.materialDesc   = rr.value().material;
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

            SlangCompiler compiler;
            if (!compiler.isValid())
                return (err("failed to initialize Slang"), 1);

            std::vector<v1::LibraryEntry> entries;
            size_t                        fileCount = 0, variantCount = 0;
            for (const auto& de : fs::recursive_directory_iterator(root))
            {
                if (!de.is_regular_file() || de.path().extension() != ".slang")
                    continue;
                ++fileCount;
                std::string source;
                if (!read_file(de.path().string(), source))
                    return (err("cannot read " + de.path().string()), 1);

                // stable id = path relative to root without extension, forward slashes
                std::string rel = fs::relative(de.path(), root).generic_string();
                if (rel.size() > 6 && rel.substr(rel.size() - 6) == ".slang")
                    rel.resize(rel.size() - 6);

                ShaderBuildOptions bo;
                bo.shaderId          = rel;
                bo.compile.emitWgsl  = !args.has("--no-wgsl");
                bo.compile.searchDirs.push_back(de.path().parent_path().string());
                bo.compile.searchDirs.push_back(root);
                for (const auto& d : extraI)
                    bo.compile.searchDirs.push_back(d);
                if (!vkwPath.empty())
                    bo.engineKeywords = &engineKw;

                auto br = build_shader(compiler, de.path().stem().string(), de.path().filename().string(),
                                       source, bo);
                if (!br.isOk())
                    return (err(rel + ": " + br.error().message), 1);

                for (const auto& v : br.value().variants)
                {
                    auto bin = v1::write_binary(to_shader_binary(v, br.value().shaderIdHash, br.value().keywords));
                    if (!bin.isOk())
                        return (err("serialize failed for " + rel), 1);
                    entries.push_back({v.variantHash, v.stage, bin.value()});
                    ++variantCount;
                }
            }

            auto libBytes = v1::write_library(entries, vkwBytes.empty() ? nullptr : &vkwBytes);
            if (!libBytes.isOk() || !write_file(out, libBytes.value()))
                return (err("failed to write " + out), 1);
            std::printf("built %zu shader(s), %zu variant(s) -> %s\n", fileCount, variantCount, out.c_str());
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

        void usage()
        {
            std::printf("vshaderc (v1.0, Slang)\n"
                        "  compile -i <in.slang> -o <out.vshbin> [-S <stage>] [-I <dir>] [-D K=V] [--no-wgsl] [--id <id>]\n"
                        "  build --shader_root <dir> -o <out.vshlib> [--keywords-file <vkw>] [-I <dir>] [--no-wgsl]\n"
                        "  pack-slang --root <dir> -o <out.vshslang> [--ext .slang]\n");
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
        if (cmd == "compile")
            return cmd_compile(args);
        if (cmd == "build")
            return cmd_build(args);
        if (cmd == "pack-slang")
            return cmd_pack_slang(args);
        if (cmd == "-h" || cmd == "--help" || cmd == "help")
            return (usage(), 0);
        err("unknown command: " + cmd);
        usage();
        return 2;
    }
} // namespace vshaderc::cli
