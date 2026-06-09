#include <vshadersystem/tool_c_api.h>
#include <vshadersystem/tool_cli.hpp>

#include <vshadersystem/binary.hpp>
#include <vshadersystem/engine_keywords.hpp>
#include <vshadersystem/hash.hpp>
#include <vshadersystem/keyword_expr.hpp>
#include <vshadersystem/library.hpp>
#include <vshadersystem/log.hpp>
#include <vshadersystem/metadata.hpp>
#include <vshadersystem/result.hpp>
#include <vshadersystem/system.hpp>
#include <vshadersystem/wgsl.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace vshadersystem;

namespace vshadersystem::tool
{

// ============================================================
// Logging
// ============================================================

static bool g_verbose = false;

static void log_info(const std::string& s) { VSS_LOG_TAG_INFO("vshaderc", "%s", s.c_str()); }

static void log_verbose(const std::string& s)
{
    if (g_verbose)
        VSS_LOG_TAG_DEBUG("vshaderc", "%s", s.c_str());
}

static void log_warn(const std::string& s) { VSS_LOG_TAG_WARN("vshaderc", "%s", s.c_str()); }

static void log_error(const std::string& s) { VSS_LOG_TAG_ERROR("vshaderc", "%s", s.c_str()); }

// Geometry shaders cannot be lowered to WGSL (WebGPU has no geometry stage). When
// targeting WebGPU we warn and skip WGSL emission for the geometry stage instead of
// failing the build, leaving bin.wgsl empty (the SPIR-V is still written for desktop).
static bool wgsl_skip_geometry(const ShaderBinary& bin)
{
    if (bin.stage == ShaderStage::eGeom)
    {
        log_warn("webgpu: geometry stage unsupported on WebGPU target; skipping WGSL emission");
        return true;
    }
    return false;
}

// ============================================================
// Usage
// ============================================================

static void print_usage()
{
    std::cout <<
        R"(vshaderc - offline shader compiler

Usage:
  vshaderc compile -i <input.vshader> -o <output.vshbin> -S <stage> [options]
  vshaderc compile --webgpu -i <input.vshader> -o <output.vshwebbin> -S <stage> [options]
  vshaderc build --shader_root <dir> [--shader <path> ...] [-I <dir> ...] [--keywords-file <path.vkw>] -o <output.vshlib> [options]
  vshaderc build --webgpu --shader_root <dir> [--shader <path> ...] [-I <dir> ...] [--keywords-file <path.vkw>] -o <output.vshweblib> [options]
  vshaderc packlib -o <output.vshlib> [--keywords-file <path.vkw>] <in1.vshbin> <in2.vshbin> ...
  vshaderc packlib --webgpu -o <output.vshweblib> [--keywords-file <path.vkw>] <in1.vshwebbin> <in2.vshwebbin> ...
  vshaderc wgsl -i <input.vshbin|input.vshwebbin|input.spv> -o <output.wgsl>

Stages:
  vert, frag, geom, comp, task, mesh, rgen, rmiss, rchit, rahit, rint

Options (compile):
  -I <dir>               Add include directory (repeatable)
  -D <NAME=VALUE>        Define macro (repeatable; VALUE optional)
  --material-mode <m>    Material access mode: bda|ubo|ssbo|push
  --language <l>         Shader source language: auto|glsl
  --webgpu               Enable WebGPU profile checks and force .vshwebbin output
  --keywords-file <vkw>  Load engine_keywords.vkw and inject global permute values if shader declares them
  --no-cache             Disable cache
  --cache <dir>          Cache directory (default: .vshader_cache)
  --dump                 On compile error, print a 10-line source context (default: on)
  --no-dump              Disable source context dump
  --dump-context <N>     Context lines above/below (default: 5)
  --emit-intermediate <dir>  Write final GLSL to <dir> on error (and on success with --emit-always)
  --emit-always          Emit final GLSL even on success
  --verbose              Verbose logging

Options (build):
  --shader_root <dir>    Root directory used for scanning shaders and computing stable shader ids
  --shader <path>        Build only a specific shader (repeatable). Path is relative to --shader_root unless absolute.
  -I <dir>               Add include directory (repeatable)
  --webgpu               Enable WebGPU profile checks and require .vshweblib output
  --keywords-file <vkw>  Load engine keywords (.vkw) and embed it into the output vshlib
  --no-cache             Disable cache
  --cache <dir>          Cache directory (default: .vshader_cache)
  --skip-invalid          Skip variants failing only_if constraints
  --dump                  On compile error, print a 10-line source context (default: on)
  --no-dump               Disable source context dump
  --dump-context <N>      Context lines above/below (default: 5)
  --emit-intermediate <dir>  Write final GLSL to <dir> on error (and on success with --emit-always)
  --emit-always           Emit final GLSL even on success
  --verbose               Verbose logging

Options (packlib):
  --webgpu               Require .vshwebbin inputs and force .vshweblib output
  --keywords-file <vkw>  Embed keywords file bytes into output vshlib
  --verbose              Verbose logging

Options (wgsl):
  -i <path>              Input .vshbin/.vshwebbin or raw SPIR-V binary (.spv)
  -o <path>              Output .wgsl text file
  --verbose              Verbose logging

Notes:
  - build infers the shader stage from filename suffix: *.vert.vshader, *.frag.vshader, *.comp.vshader, ...

Examples:
  vshaderc compile -i shaders/pbr.frag.vshader -o out/pbr.frag.vshbin -S frag -I shaders/include -D USE_FOO=1
  vshaderc compile --webgpu -i shaders/pbr.frag.vshader -o out/pbr.frag.vshwebbin -S frag -I shaders/include
  vshaderc build --shader_root examples/keywords/shaders --keywords-file examples/keywords/engine_keywords.vkw -o out/shaders.vshlib --verbose
  vshaderc packlib -o out/shaders.vshlib --keywords-file engine_keywords.vkw out/*.vshbin
  vshaderc packlib --webgpu -o out/shaders.vshweblib out/*.vshwebbin
  vshaderc wgsl -i out/pbr.frag.vshbin -o out/pbr.frag.wgsl
)";
}

// ============================================================
// Utility
// ============================================================

static inline std::string trim_copy(std::string s)
{
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

static inline std::string normalize_path_slashes(std::string s)
{
    for (auto& c : s)
        if (c == '\\')
            c = '/';
    return s;
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

static bool read_binary_file(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    out.resize(size);
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

static bool write_text_file(const std::string& path, const std::string& text)
{
    std::filesystem::path p(path);
    if (!p.parent_path().empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec)
            return false;
    }

    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

static Result<std::vector<uint32_t>> read_spirv_words_auto(const std::string& inPath)
{
    std::vector<uint8_t> bytes;
    if (!read_binary_file(inPath, bytes))
        return Result<std::vector<uint32_t>>::err({ErrorCode::eIO, "Failed to read input file: " + inPath});

    if (inPath.ends_with(".vshbin") || inPath.ends_with(".vshwebbin"))
    {
        auto bin = read_vshbin(bytes);
        if (!bin.isOk())
            return Result<std::vector<uint32_t>>::err(bin.error());
        return Result<std::vector<uint32_t>>::ok(std::move(bin.value().spirv));
    }

    if ((bytes.size() % 4) != 0)
    {
        return Result<std::vector<uint32_t>>::err(
            {ErrorCode::eParseError, "Raw SPIR-V binary size is not 4-byte aligned."});
    }

    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return Result<std::vector<uint32_t>>::ok(std::move(words));
}

static bool split_list(const std::string& s, std::vector<std::string>& out)
{
    out.clear();
    std::string cur;
    for (char c : s)
    {
        if (c == ';' || c == ',')
        {
            cur = trim_copy(cur);
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    cur = trim_copy(cur);
    if (!cur.empty())
        out.push_back(cur);
    return true;
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
    if (s == "geom")
    {
        out = ShaderStage::eGeom;
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

static bool parse_defines_kv_list(const std::string& s, std::vector<Define>& out)
{
    out.clear();
    std::vector<std::string> parts;
    split_list(s, parts);

    for (auto& p : parts)
    {
        auto   eq = p.find('=');
        Define d;
        if (eq == std::string::npos)
        {
            d.name  = trim_copy(p);
            d.value = "";
        }
        else
        {
            d.name  = trim_copy(p.substr(0, eq));
            d.value = trim_copy(p.substr(eq + 1));
        }
        if (!d.name.empty())
            out.push_back(std::move(d));
    }
    return true;
}

static inline std::string normalize_define_set(const std::vector<Define>& defs)
{
    std::vector<std::string> lines;
    lines.reserve(defs.size());
    for (const auto& d : defs)
        lines.push_back(d.value.empty() ? d.name : (d.name + "=" + d.value));
    std::sort(lines.begin(), lines.end());
    std::string out;
    for (auto& s : lines)
    {
        out += s;
        out.push_back(';');
    }
    return out;
}

// ============================================================
// Keyword value parsing (for only_if resolve)
// ============================================================

static inline bool parse_bool_str(const std::string& s, uint32_t& out)
{
    if (s.empty())
    {
        out = 1;
        return true;
    }
    if (s == "1" || s == "true" || s == "TRUE" || s == "True")
    {
        out = 1;
        return true;
    }
    if (s == "0" || s == "false" || s == "FALSE" || s == "False")
    {
        out = 0;
        return true;
    }
    return false;
}

static inline Result<uint32_t> parse_keyword_value_local(const KeywordDecl& d, const std::string& raw)
{
    if (d.kind == KeywordValueKind::eBool)
    {
        uint32_t v = 0;
        if (!parse_bool_str(raw, v))
            return Result<uint32_t>::err({ErrorCode::eParseError, "Invalid bool for keyword '" + d.name + "'"});
        return Result<uint32_t>::ok(v);
    }

    // Enum:
    if (raw.empty())
        return Result<uint32_t>::ok(d.defaultValue);

    // numeric index
    if (std::all_of(raw.begin(), raw.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
    {
        uint32_t idx = 0;
        for (char c : raw)
            idx = idx * 10u + static_cast<uint32_t>(c - '0');
        if (idx >= d.enumValues.size())
            return Result<uint32_t>::err({ErrorCode::eParseError, "Enum index out of range for '" + d.name + "'"});
        return Result<uint32_t>::ok(idx);
    }

    for (uint32_t i = 0; i < static_cast<uint32_t>(d.enumValues.size()); ++i)
        if (d.enumValues[i] == raw)
            return Result<uint32_t>::ok(i);

    return Result<uint32_t>::err({ErrorCode::eParseError, "Unknown enum value '" + raw + "' for '" + d.name + "'"});
}

static ShaderStage stage_from_filename(const std::string& name)
{
    if (name.ends_with(".vert"))
        return ShaderStage::eVert;

    if (name.ends_with(".frag"))
        return ShaderStage::eFrag;

    if (name.ends_with(".geom"))
        return ShaderStage::eGeom;

    if (name.ends_with(".comp"))
        return ShaderStage::eComp;

    if (name.ends_with(".mesh"))
        return ShaderStage::eMesh;

    if (name.ends_with(".task"))
        return ShaderStage::eTask;

    return ShaderStage::eUnknown;
}

// ============================================================
// packlib
// ============================================================

static int cmd_packlib(int argc, char** argv)
{
    // vshaderc packlib -o out/shaders.vshlib [--keywords-file path.vkw] <in1.vshbin> <in2.vshbin> ...
    std::string              outPath;
    std::string              keywordsPath;
    std::vector<std::string> inputs;
    bool                     webgpu  = false;
    bool                     verbose = false;

    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)
        {
            outPath = argv[++i];
        }
        else if ((a == "--keywords-file" || a.rfind("--keywords-file=", 0) == 0))
        {
            if (a == "--keywords-file")
            {
                if (i + 1 >= argc)
                {
                    log_error("--keywords-file requires a path");
                    return 2;
                }
                keywordsPath = argv[++i];
            }
            else
            {
                keywordsPath = a.substr(std::string("--keywords-file=").size());
            }
        }
        else if (a == "--verbose")
        {
            verbose = true;
        }
        else if (a == "--webgpu")
        {
            webgpu = true;
        }
        else if (!a.empty() && a[0] == '-')
        {
            log_error("Unknown packlib arg: " + a);
            return 2;
        }
        else
        {
            inputs.push_back(std::move(a));
        }
    }

    g_verbose = verbose;

    if (outPath.empty() || inputs.empty())
    {
        log_error("packlib: output (-o) and at least one input .vshbin are required.");
        return 2;
    }

    if (webgpu)
    {
        if (!outPath.ends_with(".vshweblib"))
        {
            std::filesystem::path p(outPath);
            p.replace_extension(".vshweblib");
            log_info("packlib: --webgpu forcing output extension to .vshweblib: " + p.generic_string());
            outPath = p.generic_string();
        }
    }

    std::vector<uint8_t> keywordsBytes;
    if (!keywordsPath.empty())
    {
        auto kw = load_engine_keywords_vkw(keywordsPath);
        if (!kw.isOk())
        {
            log_error("packlib: failed to parse keywords file: " + kw.error().message);
            return 3;
        }
        if (!read_binary_file(keywordsPath, keywordsBytes))
        {
            log_error("packlib: failed to read keywords file bytes: " + keywordsPath);
            return 3;
        }
        log_info("packlib: embedding keywords file: " + keywordsPath);
    }

    std::vector<ShaderLibraryEntry> entries;
    entries.reserve(inputs.size());

    std::unordered_set<uint64_t> seen;
    seen.reserve(inputs.size() * 2);

    for (const auto& path : inputs)
    {
        if (webgpu)
        {
            if (!path.ends_with(".vshwebbin"))
            {
                log_error("packlib: --webgpu requires .vshwebbin inputs. Invalid: " + path);
                return 4;
            }
        }
        else
        {
            if (path.ends_with(".vshwebbin"))
            {
                log_error("packlib: .vshwebbin input requires --webgpu: " + path);
                return 4;
            }
        }

        auto r = read_vshbin_file(path);
        if (!r.isOk())
        {
            log_error("packlib: failed to read " + path + ": " + r.error().message);
            return 4;
        }

        const auto& bin = r.value();
        if (webgpu && bin.wgsl.empty())
        {
            log_error("packlib: missing WGSL payload in webgpu input: " + path);
            return 4;
        }

        ShaderLibraryEntry e;
        e.keyHash = (bin.variantHash != 0) ? bin.variantHash : bin.contentHash;
        e.stage   = bin.stage;

        log_verbose("processing " + path + " shaderIdHash=" + std::to_string(bin.shaderIdHash) + " contentHash=" +
                    std::to_string(bin.contentHash) + " variantHash=" + std::to_string(bin.variantHash) +
                    " stage=" + std::to_string(static_cast<int>(bin.stage)));

        const uint64_t sig =
            xxhash64(&e.keyHash, sizeof(e.keyHash), static_cast<uint64_t>(static_cast<uint8_t>(e.stage)));
        if (seen.find(sig) != seen.end())
        {
            log_error("packlib: duplicate entry for keyHash=" + std::to_string(e.keyHash) +
                      " stage=" + std::to_string(static_cast<int>(e.stage)) + " input=" + path);
            return 4;
        }
        seen.insert(sig);

        if (!read_binary_file(path, e.blob))
        {
            log_error("packlib: failed to read bytes for " + path);
            return 4;
        }

        entries.push_back(std::move(e));
    }

    // deterministic order
    std::sort(entries.begin(), entries.end(), [](const ShaderLibraryEntry& a, const ShaderLibraryEntry& b) {
        if (a.keyHash != b.keyHash)
            return a.keyHash < b.keyHash;
        return static_cast<uint8_t>(a.stage) < static_cast<uint8_t>(b.stage);
    });

    auto w = write_vslib(outPath, entries, keywordsBytes.empty() ? nullptr : &keywordsBytes);
    if (!w.isOk())
    {
        log_error("packlib: write failed: " + w.error().message);
        return 5;
    }

    log_info("packlib: wrote " + outPath + " (" + std::to_string(entries.size()) + " entries)");
    return 0;
}

// ============================================================
// Cook manifest structs
// ============================================================

static int cmd_wgsl(int argc, char** argv)
{
    std::string inPath;
    std::string outPath;
    bool        verbose = false;

    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")
        {
            print_usage();
            return 0;
        }
        if (a == "-i" && i + 1 < argc)
        {
            inPath = argv[++i];
            continue;
        }
        if (a == "-o" && i + 1 < argc)
        {
            outPath = argv[++i];
            continue;
        }
        if (a == "--verbose")
        {
            verbose = true;
            continue;
        }

        log_error("Unknown wgsl argument: " + a);
        return 2;
    }

    g_verbose = verbose;

    if (inPath.empty() || outPath.empty())
    {
        log_error("wgsl: input (-i) and output (-o) are required.");
        return 2;
    }

    auto spv = read_spirv_words_auto(inPath);
    if (!spv.isOk())
    {
        log_error("wgsl: failed to load SPIR-V: " + spv.error().message);
        return 3;
    }

    auto wgsl = spirv_to_wgsl(spv.value());
    if (!wgsl.isOk())
    {
        log_error("wgsl: conversion failed: " + wgsl.error().message);
        return 4;
    }

    if (!write_text_file(outPath, wgsl.value()))
    {
        log_error("wgsl: failed to write output: " + outPath);
        return 5;
    }

    log_info("wgsl: wrote " + outPath);
    return 0;
}

static int cmd_compile(int argc, char** argv)
{
    // vshaderc compile -i <input> -o <out.vshbin> -S <stage> [options]
    std::string              inPath;
    std::string              outPath;
    std::string              stageStr;
    std::vector<std::string> includeDirs;
    std::vector<Define>      defines;
    std::string              keywordsFile;
    bool                     webgpu          = false;
    bool                     enableCache     = true;
    std::string              cacheDir        = ".vshader_cache";
    bool                     verbose         = false;
    std::string              materialModeStr = "bda";
    std::string              languageStr     = "auto";
    bool                     dumpOnError     = true;
    int                      dumpContext     = 5;
    std::string              emitIntermediateDir;
    bool                     emitAlways = false;

    for (int i = 2; i < argc; ++i)
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
        else if ((a == "--keywords-file" || a.rfind("--keywords-file=", 0) == 0))
        {
            if (a == "--keywords-file")
            {
                if (i + 1 >= argc)
                {
                    log_error("--keywords-file requires a path");
                    return 2;
                }
                keywordsFile = argv[++i];
            }
            else
            {
                keywordsFile = a.substr(std::string("--keywords-file=").size());
            }
        }
        else if (a == "--no-cache")
        {
            enableCache = false;
        }
        else if ((a == "--material-mode" || a.rfind("--material-mode=", 0) == 0))
        {
            if (a == "--material-mode")
            {
                if (i + 1 >= argc)
                {
                    log_error("--material-mode requires bda|ubo|ssbo|push");
                    return 2;
                }
                materialModeStr = argv[++i];
            }
            else
            {
                materialModeStr = a.substr(std::strlen("--material-mode="));
            }
        }
        else if ((a == "--language" || a.rfind("--language=", 0) == 0))
        {
            if (a == "--language")
            {
                if (i + 1 >= argc)
                {
                    log_error("--language requires auto|glsl");
                    return 2;
                }
                languageStr = argv[++i];
            }
            else
            {
                languageStr = a.substr(std::strlen("--language="));
            }
        }
        else if (a == "--webgpu")
        {
            webgpu = true;
        }
        else if (a == "--cache" && i + 1 < argc)
        {
            cacheDir = argv[++i];
        }
        else if (a == "--verbose")
        {
            verbose = true;
        }
        else if (a == "--no-dump")
        {
            dumpOnError = false;
        }
        else if (a == "--dump")
        {
            dumpOnError = true;
        }
        else if (a == "--dump-context" && i + 1 < argc)
        {
            dumpContext = std::max(0, std::atoi(argv[++i]));
        }
        else if (a == "--emit-intermediate" && i + 1 < argc)
        {
            emitIntermediateDir = argv[++i];
        }
        else if (a == "--emit-always")
        {
            emitAlways = true;
        }
        else
        {
            log_error("Unknown compile argument: " + a);
            print_usage();
            return 2;
        }
    }

    g_verbose = verbose;

    ShaderStage stage = ShaderStage::eFrag;
    if (!parse_stage(stageStr, stage))
    {
        log_error("Invalid stage: " + stageStr);
        return 3;
    }

    if (inPath.empty() || outPath.empty())
    {
        log_error("compile: input/output must be specified (-i/-o)");
        return 4;
    }

    if (webgpu)
    {
        if (materialModeStr == "bda")
        {
            log_error("compile: --webgpu does not support --material-mode=bda. Use ubo/ssbo/push.");
            return 2;
        }

        if (!outPath.ends_with(".vshwebbin"))
        {
            std::filesystem::path p(outPath);
            p.replace_extension(".vshwebbin");
            log_info("compile: --webgpu forcing output extension to .vshwebbin: " + p.generic_string());
            outPath = p.generic_string();
        }
    }

    std::string src;
    if (!read_text_file(inPath, src))
    {
        log_error("compile: failed to read input file: " + inPath);
        return 5;
    }

    EngineKeywordsFile engineKw;
    bool               hasEngineKw = false;
    if (!keywordsFile.empty())
    {
        auto kwr = load_engine_keywords_vkw(keywordsFile);
        if (!kwr.isOk())
        {
            log_error("compile: failed to parse keywords file: " + kwr.error().message);
            return 5;
        }
        engineKw    = std::move(kwr.value());
        hasEngineKw = true;

        // Parse shader metadata to discover declared keywords for injection.
        auto mr = parse_vultra_metadata(src);
        if (!mr.isOk())
        {
            log_error("compile: failed to parse shader metadata for keyword injection: " + mr.error().message);
            return 5;
        }

        // Build define map: do not override user -D
        std::unordered_map<std::string, std::string> defMap;
        defMap.reserve(defines.size());
        for (const auto& d : defines)
            defMap[d.name] = d.value;

        for (const auto& kd : mr.value().keywords)
        {
            if (kd.dispatch != KeywordDispatch::ePermutation)
                continue;
            if (kd.scope != KeywordScope::eGlobal)
                continue;
            if (defMap.find(kd.name) != defMap.end())
                continue;

            auto iv = engineKw.values.find(kd.name);
            if (iv != engineKw.values.end())
            {
                Define d;
                d.name  = kd.name;
                d.value = iv->second;
                defines.push_back(std::move(d));
                defMap[kd.name] = iv->second;
            }
        }
    }

    BuildRequest req;
    req.source.virtualPath  = inPath;
    req.source.sourceText   = std::move(src);
    req.options.stage       = stage;
    req.options.includeDirs = std::move(includeDirs);
    req.options.defines     = std::move(defines);

    // Diagnostics
    req.options.dumpSourceOnError      = dumpOnError;
    req.options.dumpContextLines       = dumpContext;
    req.options.emitIntermediateDir    = emitIntermediateDir;
    req.options.emitIntermediateAlways = emitAlways;
    req.options.webgpuProfile          = webgpu;

    // Language
    if (languageStr == "slang")
    {
        log_error("Slang is no longer supported in v0.6.0+. Please migrate to GLSL.");
        return 1;
    }

    if (languageStr == "glsl")
        req.options.language = ShaderLanguage::eGLSL;
    else
        req.options.language = ShaderLanguage::eAuto;

    // Material access mode
    if (materialModeStr == "bda")
        req.options.materialAccessMode = MaterialAccessMode::eBDA;
    else if (materialModeStr == "ubo")
        req.options.materialAccessMode = MaterialAccessMode::eUBO;
    else if (materialModeStr == "ssbo")
        req.options.materialAccessMode = MaterialAccessMode::eSSBO;
    else if (materialModeStr == "push")
        req.options.materialAccessMode = MaterialAccessMode::ePushConstant;
    else
    {
        log_error("Unknown --material-mode: " + materialModeStr);
        return 1;
    }

    req.hasEngineKeywords = hasEngineKw;
    if (hasEngineKw)
        req.engineKeywords = std::move(engineKw);

    req.enableCache = enableCache;
    req.cacheDir    = cacheDir;

    ShaderStage inferred = stage_from_filename(inPath);

    auto start = std::chrono::steady_clock::now();

    if (inferred != ShaderStage::eUnknown)
    {
        req.options.stage = inferred;

        auto r = build_single_shader(req);

        auto end = std::chrono::steady_clock::now();

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        log_info("compile: build_single_shader took " + std::to_string(ms) + " ms");

        if (!r.isOk())
        {
            log_error("compile: build failed: " + r.error().message);
            return 6;
        }

        ShaderBinary bin = r.value().binary;
        if (webgpu && !wgsl_skip_geometry(bin))
        {
            auto wg = spirv_to_wgsl(bin.spirv);
            if (!wg.isOk())
            {
                log_error("compile: webgpu conversion failed: " + wg.error().message);
                return 6;
            }
            bin.wgsl = std::move(wg.value());
        }

        auto w = write_vshbin_file(outPath, bin);

        if (!w.isOk())
        {
            log_error("compile: write failed: " + w.error().message);
            return 7;
        }
    }
    else
    {
        auto r = build_multiple_shaders(req);

        auto end = std::chrono::steady_clock::now();

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        log_info("compile: build_multiple_shaders took " + std::to_string(ms) + " ms");

        if (!r.isOk())
        {
            log_error("compile: build failed: " + r.error().message);
            return 6;
        }

        std::filesystem::path base = outPath;

        base.replace_extension("");

        for (auto& [stage, result] : r.value())
        {

            std::string ext;

            switch (stage)
            {

                case ShaderStage::eVert:
                    ext = "vert";
                    break;
                case ShaderStage::eFrag:
                    ext = "frag";
                    break;
                case ShaderStage::eGeom:
                    ext = "geom";
                    break;
                case ShaderStage::eComp:
                    ext = "comp";
                    break;
                case ShaderStage::eMesh:
                    ext = "mesh";
                    break;
                case ShaderStage::eTask:
                    ext = "task";
                    break;

                case ShaderStage::eRgen:
                    ext = "rgen";
                    break;
                case ShaderStage::eRmiss:
                    ext = "rmiss";
                    break;
                case ShaderStage::eRchit:
                    ext = "rchit";
                    break;
                case ShaderStage::eRahit:
                    ext = "rahit";
                    break;
                case ShaderStage::eRint:
                    ext = "rint";
                    break;

                default:
                    ext = "unknown";
            }

            auto outFile = base.string() + "." + ext + (webgpu ? ".vshwebbin" : ".vshbin");

            ShaderBinary bin = result.binary;
            if (webgpu && !wgsl_skip_geometry(bin))
            {
                auto wg = spirv_to_wgsl(bin.spirv);
                if (!wg.isOk())
                {
                    log_error("compile: webgpu conversion failed for stage " + ext + ": " + wg.error().message);
                    return 6;
                }
                bin.wgsl = std::move(wg.value());
            }

            auto w = write_vshbin_file(outFile, bin);

            if (!w.isOk())
            {
                log_error("compile: write failed: " + w.error().message);
                return 7;
            }

            log_info("compile: wrote " + outFile);
        }
    }

    return 0;
}

// ============================================================
// build
// ============================================================

static bool infer_stage_from_shader_path(const std::filesystem::path& p, ShaderStage& outStage)
{
    auto ext = p.extension().string();

    if (ext != ".vshader")
        return false;

    auto stem = p.stem().string();

    auto ext2 = std::filesystem::path(stem).extension().string();

    // *.vshader (single file, multiple stages)
    // will be handled by build_multiple_shaders, which parses the file for stage markers.
    if (ext2.empty())
        return false;

    if (ext2.size() <= 1)
        return false;

    return parse_stage(ext2.substr(1), outStage);
}

static void scan_shader_root(const std::filesystem::path& root, std::vector<std::filesystem::path>& outFiles)
{
    outFiles.clear();

    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
        return;

    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec))
    {
        if (ec)
            break;

        const auto& e = *it;
        if (!e.is_regular_file(ec))
            continue;

        const auto& p = e.path();

        // Keep this conservative: we mainly build *.vshader.
        if (p.extension() == ".vshader")
        {
            outFiles.push_back(p);
            continue;
        }
    }

    std::sort(outFiles.begin(), outFiles.end());
}

static void enumerate_permutation_variants(const std::vector<const KeywordDecl*>& permuteDecls,
                                           size_t                                 idx,
                                           std::vector<Define>&                   cur,
                                           std::vector<std::vector<Define>>&      out)
{
    if (idx >= permuteDecls.size())
    {
        out.push_back(cur);
        return;
    }

    const KeywordDecl* kd = permuteDecls[idx];

    // Bool: {0,1}
    if (kd->kind == KeywordValueKind::eBool)
    {
        // 0
        {
            Define d;
            d.name  = kd->name;
            d.value = "0";
            cur.push_back(std::move(d));
            enumerate_permutation_variants(permuteDecls, idx + 1, cur, out);
            cur.pop_back();
        }
        // 1
        {
            Define d;
            d.name  = kd->name;
            d.value = "1";
            cur.push_back(std::move(d));
            enumerate_permutation_variants(permuteDecls, idx + 1, cur, out);
            cur.pop_back();
        }
        return;
    }

    // Enum: use enumerant strings
    for (const auto& ev : kd->enumValues)
    {
        Define d;
        d.name  = kd->name;
        d.value = ev;
        cur.push_back(std::move(d));
        enumerate_permutation_variants(permuteDecls, idx + 1, cur, out);
        cur.pop_back();
    }
}

static int cmd_build(int argc, char** argv)
{
    // vshaderc build --shader_root <dir> [--shader <path> ...] [-I <dir> ...] [--keywords-file <vkw>] -o <vshlib>
    // [--cache dir] [--no-cache] [--skip-invalid] [--verbose]
    std::string              shaderRoot;
    std::vector<std::string> shaders;
    std::vector<std::string> includeDirs;
    std::string              keywordsPath;
    std::string              outLibPath;
    bool                     webgpu          = false;
    bool                     enableCache     = true;
    std::string              cacheDir        = ".vshader_cache";
    bool                     skipInvalid     = false;
    bool                     verbose         = false;
    std::string              materialModeStr = "bda";
    std::string              languageStr     = "auto";
    bool                     dumpOnError     = true;
    int                      dumpContext     = 5;
    std::string              emitIntermediateDir;
    bool                     emitAlways = false;
    std::vector<VfsMount>    mounts; // --mount <name>=<file.vshglsl>

    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];

        if (a == "--shader_root" && i + 1 < argc)
        {
            shaderRoot = normalize_path_slashes(argv[++i]);
        }
        else if (a == "--shader" && i + 1 < argc)
        {
            shaders.push_back(normalize_path_slashes(argv[++i]));
        }
        else if (a == "-I" && i + 1 < argc)
        {
            includeDirs.push_back(normalize_path_slashes(argv[++i]));
        }
        else if (a == "--mount" && i + 1 < argc)
        {
            // --mount <name>=<file.vshglsl> : mount a packed GLSL library at <name>.
            const std::string spec = argv[++i];
            const size_t      eq   = spec.find('=');
            if (eq == std::string::npos)
            {
                log_error("--mount requires <name>=<file.vshglsl>");
                return 1;
            }
            const std::string mountName = spec.substr(0, eq);
            const std::string libPath   = spec.substr(eq + 1);
            auto              libR      = read_glsl_library_file(libPath);
            if (!libR.isOk())
            {
                log_error("--mount: failed to read glsl library '" + libPath + "': " + libR.error().message);
                return 1;
            }
            VfsMount m;
            m.mount = mountName;
            for (auto& file : libR.value())
                m.files.push_back(VirtualIncludeFile {std::move(file.virtualPath), std::move(file.sourceText)});
            mounts.push_back(std::move(m));
        }
        else if ((a == "--material-mode" || a.rfind("--material-mode=", 0) == 0))
        {
            if (a == "--material-mode")
            {
                if (i + 1 >= argc)
                {
                    log_error("--material-mode requires bda|ubo|ssbo|push");
                    return 1;
                }
                materialModeStr = argv[++i];
            }
            else
            {
                materialModeStr = a.substr(std::strlen("--material-mode="));
            }
        }
        else if ((a == "--language" || a.rfind("--language=", 0) == 0))
        {
            if (a == "--language")
            {
                if (i + 1 >= argc)
                {
                    log_error("--language requires auto|glsl");
                    return 1;
                }
                languageStr = argv[++i];
            }
            else
            {
                languageStr = a.substr(std::strlen("--language="));
            }
        }
        else if ((a == "--keywords-file" || a.rfind("--keywords-file=", 0) == 0))
        {
            if (a == "--keywords-file")
            {
                if (i + 1 >= argc)
                {
                    log_error("--keywords-file requires a path");
                    return 2;
                }
                keywordsPath = argv[++i];
            }
            else
            {
                keywordsPath = a.substr(std::string("--keywords-file=").size());
            }
        }
        else if (a == "-o" && i + 1 < argc)
        {
            outLibPath = argv[++i];
        }
        else if (a == "--webgpu")
        {
            webgpu = true;
        }
        else if (a == "--no-cache")
        {
            enableCache = false;
        }
        else if (a == "--cache" && i + 1 < argc)
        {
            cacheDir = argv[++i];
        }
        else if (a == "--skip-invalid")
        {
            skipInvalid = true;
        }
        else if (a == "--verbose")
        {
            verbose = true;
        }
        else if (a == "--no-dump")
        {
            dumpOnError = false;
        }
        else if (a == "--dump")
        {
            dumpOnError = true;
        }
        else if (a == "--dump-context" && i + 1 < argc)
        {
            dumpContext = std::max(0, std::atoi(argv[++i]));
        }
        else if (a == "--emit-intermediate" && i + 1 < argc)
        {
            emitIntermediateDir = argv[++i];
        }
        else if (a == "--emit-always")
        {
            emitAlways = true;
        }
        else if (a == "-h" || a == "--help")
        {
            print_usage();
            return 0;
        }
        else
        {
            log_error("Unknown build arg: " + a);
            return 2;
        }
    }

    g_verbose = verbose;

    if (shaderRoot.empty())
    {
        log_error("build: --shader_root <dir> is required");
        return 2;
    }
    if (outLibPath.empty())
    {
        log_error("build: -o <output.vshlib> is required");
        return 2;
    }

    if (webgpu)
    {
        if (materialModeStr == "bda")
        {
            log_error("build: --webgpu does not support --material-mode=bda. Use ubo/ssbo/push.");
            return 2;
        }

        if (!outLibPath.ends_with(".vshweblib"))
        {
            std::filesystem::path p(outLibPath);
            p.replace_extension(".vshweblib");
            log_info("build: --webgpu forcing output extension to .vshweblib: " + p.generic_string());
            outLibPath = p.generic_string();
        }
    }

    std::filesystem::path shaderRootPath = std::filesystem::absolute(shaderRoot);

    // Implicit include dirs: shader_root and shader_root/include if present.
    {
        includeDirs.push_back(shaderRootPath.generic_string());
        const auto      inc = shaderRootPath / "include";
        std::error_code ec;
        if (std::filesystem::exists(inc, ec))
            includeDirs.push_back(inc.generic_string());
    }

    EngineKeywordsFile   engineKw;
    bool                 hasEngineKw = false;
    std::vector<uint8_t> keywordsBytes;

    if (!keywordsPath.empty())
    {
        log_info("build: loading engine keywords: " + keywordsPath);
        auto kwr = load_engine_keywords_vkw(keywordsPath);
        if (!kwr.isOk())
        {
            log_error("build: failed to parse keywords file: " + kwr.error().message);
            return 3;
        }
        engineKw    = std::move(kwr.value());
        hasEngineKw = true;

        if (!read_binary_file(keywordsPath, keywordsBytes))
        {
            log_error("build: failed to read keywords bytes: " + keywordsPath);
            return 3;
        }
    }

    // Resolve shader list.
    std::vector<std::filesystem::path> shaderFiles;

    if (shaders.empty())
    {
        scan_shader_root(shaderRootPath, shaderFiles);
    }
    else
    {
        shaderFiles.reserve(shaders.size());
        for (const auto& s : shaders)
        {
            std::filesystem::path p = s;
            if (p.is_relative())
                p = shaderRootPath / p;
            shaderFiles.push_back(std::filesystem::weakly_canonical(p));
        }
        std::sort(shaderFiles.begin(), shaderFiles.end());
    }

    if (shaderFiles.empty())
    {
        log_error("build: no shaders found under: " + shaderRootPath.generic_string());
        return 4;
    }

    log_info("build: shaders=" + std::to_string(shaderFiles.size()));

    std::vector<ShaderLibraryEntry> entries;
    entries.reserve(1024);

    std::unordered_set<uint64_t> seen;
    seen.reserve(4096);

    size_t      pruned     = 0;
    std::string firstError = {};

    // Conflict detection: each explicit shader id must be unique across the
    // library. Maps shaderIdHash -> the source file that first claimed it.
    std::unordered_map<uint64_t, std::string> idOwner;
    const auto registerShaderId = [&](uint64_t idHash, const std::string& vpath) -> bool {
        auto [it, inserted] = idOwner.emplace(idHash, vpath);
        if (!inserted && it->second != vpath)
        {
            firstError = "build: duplicate shader id (hash " + std::to_string(idHash) + ") declared by '" +
                         it->second + "' and '" + vpath + "' - shader ids must be unique";
            return false;
        }
        return true;
    };

    size_t shaderIndex = 0;

    for (const auto& shaderPathAbs : shaderFiles)
    {
        ++shaderIndex;

        std::error_code ec;
        auto            rel = std::filesystem::relative(shaderPathAbs, shaderRootPath, ec);
        if (ec)
            rel = shaderPathAbs.filename();

        const std::string virtualPath = normalize_path_slashes(rel.generic_string());

        // NEW: stage inference is optional now.
        ShaderStage inferredStage {};
        const bool  hasInferredStage = infer_stage_from_shader_path(shaderPathAbs, inferredStage);

        log_info("build: [" + std::to_string(shaderIndex) + "/" + std::to_string(shaderFiles.size()) + "] " +
                 virtualPath + (hasInferredStage ? "" : " (multi-stage)"));

        std::string src;
        if (!read_text_file(shaderPathAbs.generic_string(), src))
        {
            firstError = "build: failed to read shader: " + shaderPathAbs.generic_string();
            break;
        }

        auto mdr = parse_vultra_metadata(src);
        if (!mdr.isOk())
        {
            firstError = "build: failed to parse metadata: " + virtualPath + ": " + mdr.error().message;
            break;
        }

        ParsedMetadata md = std::move(mdr.value());

        // Collect permutation keyword decls
        std::vector<const KeywordDecl*> permuteDecls;
        permuteDecls.reserve(md.keywords.size());

        for (const auto& kd : md.keywords)
        {
            if (kd.dispatch == KeywordDispatch::ePermutation)
                permuteDecls.push_back(&kd);
        }

        // Enumerate all combinations
        std::vector<std::vector<Define>> variantDefines;
        {
            std::vector<Define> cur;
            enumerate_permutation_variants(permuteDecls, 0, cur, variantDefines);
        }

        if (variantDefines.empty())
            variantDefines.push_back({});

        log_info("build: variants=" + std::to_string(variantDefines.size()));

        size_t variantIndex = 0;

        for (auto& defines : variantDefines)
        {
            ++variantIndex;

            // Constraint pruning (only_if)
            bool skipThisVariant = false;

            {
                KeywordValueContext ctx;
                ctx.values.reserve(md.keywords.size() * 2);
                ctx.decls.reserve(md.keywords.size() * 2);

                for (const auto& kd : md.keywords)
                    ctx.decls[kd.name] = &kd;

                std::unordered_map<std::string, std::string> defMap;
                defMap.reserve(defines.size() * 2);
                for (const auto& d : defines)
                    defMap[d.name] = d.value;

                // resolve values for all keywords: defaults -> defines -> engineKw (global only)
                for (const auto& kd : md.keywords)
                {
                    uint32_t v = kd.defaultValue;

                    auto it = defMap.find(kd.name);
                    if (it != defMap.end())
                    {
                        auto pv = parse_keyword_value_local(kd, it->second);
                        if (!pv.isOk())
                        {
                            firstError = "build: invalid keyword value for " + kd.name + " in " + virtualPath + ": " +
                                         pv.error().message;
                            break;
                        }
                        v = pv.value();
                    }
                    else if (hasEngineKw && kd.scope == KeywordScope::eGlobal)
                    {
                        auto iv = engineKw.values.find(kd.name);
                        if (iv != engineKw.values.end())
                        {
                            auto pv = parse_keyword_value_local(kd, iv->second);
                            if (!pv.isOk())
                            {
                                firstError =
                                    "build: invalid engine keyword value for " + kd.name + ": " + pv.error().message;
                                break;
                            }
                            v = pv.value();
                        }
                    }

                    ctx.values[kd.name] = v;
                }

                if (!firstError.empty())
                    break;

                for (const auto& kd : md.keywords)
                {
                    if (kd.constraint.empty())
                        continue;

                    auto er = eval_only_if(kd.constraint, ctx);
                    if (!er.isOk())
                    {
                        firstError = "build: failed to eval only_if for keyword '" + kd.name + "' in " + virtualPath +
                                     ": " + er.error().message;
                        break;
                    }
                    if (!er.value())
                    {
                        ++pruned;

                        if (!skipInvalid)
                        {
                            firstError =
                                "build: variant violates only_if constraint: " + virtualPath + " (" + kd.name + ")";
                            break;
                        }

                        skipThisVariant = true;
                        break;
                    }
                }

                if (!firstError.empty())
                    break;
            }

            if (skipThisVariant)
                continue;

            BuildRequest req;
            req.source.virtualPath  = virtualPath;
            req.source.sourceText   = src;
            req.options.includeDirs = includeDirs;
            req.options.vfsMounts   = mounts;
            req.options.defines     = defines;

            // Diagnostics
            req.options.dumpSourceOnError      = dumpOnError;
            req.options.dumpContextLines       = dumpContext;
            req.options.emitIntermediateDir    = emitIntermediateDir;
            req.options.emitIntermediateAlways = emitAlways;
            req.options.webgpuProfile          = webgpu;

            // Stage policy:
            // - If file name infers a stage, compile that single stage.
            // - Otherwise, let build_multiple_shaders decide stages from markers.
            req.options.stage = hasInferredStage ? inferredStage : ShaderStage::eUnknown;

            // Language
            if (languageStr == "slang")
            {
                log_error("Slang is no longer supported in v0.6.0+. Please migrate to GLSL.");
                return 5;
            }

            if (languageStr == "glsl")
                req.options.language = ShaderLanguage::eGLSL;
            else
                req.options.language = ShaderLanguage::eAuto;

            // Material access mode
            if (materialModeStr == "bda")
                req.options.materialAccessMode = MaterialAccessMode::eBDA;
            else if (materialModeStr == "ubo")
                req.options.materialAccessMode = MaterialAccessMode::eUBO;
            else if (materialModeStr == "ssbo")
                req.options.materialAccessMode = MaterialAccessMode::eSSBO;
            else if (materialModeStr == "push")
                req.options.materialAccessMode = MaterialAccessMode::ePushConstant;
            else
            {
                log_error("Unknown --material-mode: " + materialModeStr);
                return 5;
            }

            req.hasEngineKeywords = hasEngineKw;
            if (hasEngineKw)
                req.engineKeywords = engineKw;

            req.enableCache = enableCache;
            req.cacheDir    = cacheDir;

            log_verbose("build: compiling variant " + std::to_string(variantIndex) + "/" +
                        std::to_string(variantDefines.size()));

            // NEW: single vs multiple
            if (hasInferredStage)
            {
                auto br = build_single_shader(req);
                if (!br.isOk())
                {
                    firstError = "build: build failed for " + virtualPath + ": " + br.error().message;
                    break;
                }

                ShaderBinary bin = br.value().binary;

                if (!registerShaderId(bin.shaderIdHash, virtualPath))
                    break;

                if (webgpu && !wgsl_skip_geometry(bin))
                {
                    auto wg = spirv_to_wgsl(bin.spirv);
                    if (!wg.isOk())
                    {
                        firstError = "build: webgpu conversion failed for " + virtualPath + ": " + wg.error().message;
                        break;
                    }
                    bin.wgsl = std::move(wg.value());
                }

                ShaderLibraryEntry e;
                e.keyHash = (bin.variantHash != 0) ? bin.variantHash : bin.contentHash;
                e.stage   = bin.stage;

                const uint64_t sig =
                    xxhash64(&e.keyHash, sizeof(e.keyHash), static_cast<uint64_t>(static_cast<uint8_t>(e.stage)));

                log_info("build: building " + virtualPath + " variant " + std::to_string(variantIndex) + "/" +
                         std::to_string(variantDefines.size()) + " shaderIdHash=" + std::to_string(bin.shaderIdHash) +
                         " contentHash=" + std::to_string(bin.contentHash) + " variantHash=" +
                         std::to_string(bin.variantHash) + " stage=" + std::to_string(static_cast<int>(bin.stage)));

                auto bytes = write_vshbin(bin);
                if (!bytes.isOk())
                {
                    firstError = "build: failed to serialize vshbin for " + virtualPath + ": " + bytes.error().message;
                    break;
                }
                e.blob = std::move(bytes.value());

                if (seen.find(sig) != seen.end())
                {
                    ++pruned;
                    log_verbose("build: skipping duplicate entry for " + virtualPath + " variant " +
                                std::to_string(variantIndex) + "/" + std::to_string(variantDefines.size()) +
                                " keyHash=" + std::to_string(e.keyHash) +
                                " stage=" + std::to_string(static_cast<int>(e.stage)));
                    continue;
                }

                seen.insert(sig);
                entries.push_back(std::move(e));
            }
            else
            {
                auto mr = build_multiple_shaders(req);
                if (!mr.isOk())
                {
                    firstError = "build: build failed for " + virtualPath + ": " + mr.error().message;
                    break;
                }

                for (auto& [stage2, br2] : mr.value())
                {
                    ShaderBinary bin = br2.binary;

                    if (!registerShaderId(bin.shaderIdHash, virtualPath))
                        break;

                    if (webgpu && !wgsl_skip_geometry(bin))
                    {
                        auto wg = spirv_to_wgsl(bin.spirv);
                        if (!wg.isOk())
                        {
                            firstError =
                                "build: webgpu conversion failed for " + virtualPath + ": " + wg.error().message;
                            break;
                        }
                        bin.wgsl = std::move(wg.value());
                    }

                    ShaderLibraryEntry e;
                    e.keyHash = (bin.variantHash != 0) ? bin.variantHash : bin.contentHash;

                    // Important: stage source of truth
                    e.stage = bin.stage;

                    const uint64_t sig =
                        xxhash64(&e.keyHash, sizeof(e.keyHash), static_cast<uint64_t>(static_cast<uint8_t>(e.stage)));

                    log_info("build: building " + virtualPath + " variant " + std::to_string(variantIndex) + "/" +
                             std::to_string(variantDefines.size()) + " shaderIdHash=" +
                             std::to_string(bin.shaderIdHash) + " contentHash=" + std::to_string(bin.contentHash) +
                             " variantHash=" + std::to_string(bin.variantHash) +
                             " stage=" + std::to_string(static_cast<int>(bin.stage)));

                    auto bytes = write_vshbin(bin);
                    if (!bytes.isOk())
                    {
                        firstError =
                            "build: failed to serialize vshbin for " + virtualPath + ": " + bytes.error().message;
                        break;
                    }
                    e.blob = std::move(bytes.value());

                    if (seen.find(sig) != seen.end())
                    {
                        ++pruned;
                        log_verbose("build: skipping duplicate entry for " + virtualPath + " variant " +
                                    std::to_string(variantIndex) + "/" + std::to_string(variantDefines.size()) +
                                    " keyHash=" + std::to_string(e.keyHash) +
                                    " stage=" + std::to_string(static_cast<int>(e.stage)));
                        continue;
                    }

                    seen.insert(sig);
                    entries.push_back(std::move(e));
                }

                if (!firstError.empty())
                    break;
            }
        }

        if (!firstError.empty())
            break;
    }

    if (!firstError.empty())
    {
        log_error(firstError);
        return 6;
    }

    // Deterministic ordering for stable builds
    std::sort(entries.begin(), entries.end(), [](const ShaderLibraryEntry& a, const ShaderLibraryEntry& b) {
        if (a.keyHash != b.keyHash)
            return a.keyHash < b.keyHash;
        return static_cast<uint8_t>(a.stage) < static_cast<uint8_t>(b.stage);
    });

    // Ensure output directory exists
    auto outDir = std::filesystem::path(outLibPath).parent_path();
    if (!outDir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        if (ec)
        {
            log_error("build: failed to create output directory: " + outDir.generic_string() + ": " + ec.message());
            return 7;
        }
    }

    log_info("build: writing vshlib: " + outLibPath + " entries=" + std::to_string(entries.size()) +
             " pruned=" + std::to_string(pruned));

    auto w = write_vslib(outLibPath, entries, keywordsBytes.empty() ? nullptr : &keywordsBytes);
    if (!w.isOk())
    {
        log_error("build: write vshlib failed: " + w.error().message);
        return 8;
    }

    log_info("build: OK -> " + outLibPath);
    return 0;
}

// ============================================================
// pack-glsl: package a directory of GLSL into a mountable .vshglsl library
//   pack-glsl --root <dir> -o <out.vshglsl> [--ext .glsl] [--ext .h] ...
// Files are stored by their path relative to <dir> (forward slashes).
// ============================================================
static int cmd_pack_glsl(int argc, char** argv)
{
    std::string              root;
    std::string              outPath;
    std::vector<std::string> exts;

    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--root" && i + 1 < argc)
            root = argv[++i];
        else if ((a == "-o" || a == "--output") && i + 1 < argc)
            outPath = argv[++i];
        else if (a == "--ext" && i + 1 < argc)
            exts.push_back(argv[++i]);
        else
        {
            log_error("pack-glsl: unknown argument: " + a);
            return 1;
        }
    }

    if (root.empty() || outPath.empty())
    {
        log_error("pack-glsl requires --root <dir> and -o <out.vshglsl>");
        return 1;
    }
    if (exts.empty())
        exts = {".glsl"};

    const std::filesystem::path rootPath(root);
    std::error_code             ec;
    if (!std::filesystem::is_directory(rootPath, ec))
    {
        log_error("pack-glsl: --root is not a directory: " + root);
        return 1;
    }

    std::vector<GlslLibraryFile> files;
    for (auto it = std::filesystem::recursive_directory_iterator(
             rootPath, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator {};
         it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const auto& p   = it->path();
        const auto  ext = p.extension().string();
        if (std::find(exts.begin(), exts.end(), ext) == exts.end())
            continue;

        auto rel = std::filesystem::relative(p, rootPath, ec);
        if (ec || rel.empty())
            continue;

        std::ifstream f(p, std::ios::binary);
        if (!f)
        {
            log_error("pack-glsl: failed to read " + p.generic_string());
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();

        GlslLibraryFile entry;
        entry.virtualPath = normalize_path_slashes(rel.generic_string());
        entry.sourceText  = ss.str();
        files.push_back(std::move(entry));
    }

    auto w = write_glsl_library(outPath, files);
    if (!w.isOk())
    {
        log_error("pack-glsl: write failed: " + w.error().message);
        return 1;
    }
    log_info("pack-glsl: OK -> " + outPath + " files=" + std::to_string(files.size()));
    return 0;
}

// ============================================================
// main dispatch
// ============================================================

int run_vshaderc(int argc, char** argv)
{
    if (argc <= 1)
    {
        print_usage();
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "-h" || cmd == "--help")
    {
        print_usage();
        return 0;
    }

    if (cmd == "compile")
        return cmd_compile(argc, argv);

    if (cmd == "build")
        return cmd_build(argc, argv);

    if (cmd == "packlib")
        return cmd_packlib(argc, argv);

    if (cmd == "pack-glsl")
        return cmd_pack_glsl(argc, argv);

    if (cmd == "wgsl")
        return cmd_wgsl(argc, argv);

    // Optional backward-compat: if user runs "vshaderc -i ...", treat as compile.
    // This keeps old scripts working.
    if (!cmd.empty() && cmd[0] == '-')
    {
        // shift argv by inserting "compile" semantics: reuse cmd_compile by pretending argv[1]="compile"
        std::vector<char*> newArgv;
        newArgv.reserve(static_cast<size_t>(argc) + 1);
        newArgv.push_back(argv[0]);
        const char* compileStr = "compile";
        newArgv.push_back(const_cast<char*>(compileStr));
        for (int i = 1; i < argc; ++i)
            newArgv.push_back(argv[i]);

        return cmd_compile(static_cast<int>(newArgv.size()), newArgv.data());
    }

    log_error("Unknown command: " + cmd);
    print_usage();
    return 2;
}

} // namespace vshadersystem::tool

extern "C" int vshadersystem_tool_run_vshaderc(const VShaderSystemToolArgs* args)
{
    if (args == nullptr || args->struct_size < sizeof(VShaderSystemToolArgs))
        return 2;
    return vshadersystem::tool::run_vshaderc(args->argc, args->argv);
}
