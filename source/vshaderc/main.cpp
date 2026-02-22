#include <vshadersystem/binary.hpp>
#include <vshadersystem/engine_keywords.hpp>
#include <vshadersystem/hash.hpp>
#include <vshadersystem/keyword_expr.hpp>
#include <vshadersystem/library.hpp>
#include <vshadersystem/metadata.hpp>
#include <vshadersystem/result.hpp>
#include <vshadersystem/system.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace vshadersystem;

// ============================================================
// Logging
// ============================================================

static bool g_verbose = false;

static void log_info(const std::string& s) { std::cout << "[vshaderc] " << s << std::endl; }

static void log_verbose(const std::string& s)
{
    if (g_verbose)
        std::cout << "[vshaderc][verbose] " << s << std::endl;
}

static void log_error(const std::string& s) { std::cerr << "[vshaderc][error] " << s << std::endl; }

// ============================================================
// Usage
// ============================================================

static void print_usage()
{
    std::cout <<
        R"(vshaderc - offline shader compiler

Usage:
  vshaderc compile -i <input.vshader> -o <output.vshbin> -S <stage> [options]
  vshaderc cook -m <manifest.vcook> -o <output.vshlib> [options]
  vshaderc cook-merge -o <merged.vcook> <a.vcook> <b.vcook> ... [options]
  vshaderc packlib -o <output.vshlib> [--keywords-file <path.vkw>] <in1.vshbin> <in2.vshbin> ...

Stages:
  vert, frag, comp, task, mesh, rgen, rmiss, rchit, rahit, rint

Options (compile):
  -I <dir>               Add include directory (repeatable)
  -D <NAME=VALUE>        Define macro (repeatable; VALUE optional)
  --keywords-file <vkw>  Load engine_keywords.vkw and inject global permute values if shader declares them
  --no-cache             Disable cache
  --cache <dir>          Cache directory (default: .vshader_cache)
  --verbose              Verbose logging

Options (cook):
  -m, --manifest <vcook> Input manifest
  -o <vshlib>             Output library
  -I <dir>               Extra include directory (repeatable, appended)
  --keywords-file <vkw>  Apply engine_keywords.vkw for global permute defaults + embed into vshlib
  --no-cache             Disable cache
  --cache <dir>          Cache directory (default: .vshader_cache)
  -j, --jobs <N>         (Ignored) Cook forced single-thread (determinism, avoid deadlocks)
  --skip-invalid          Skip variants failing only_if constraints
  --verbose               Verbose pruning + entrypoint probe

Options (cook-merge):
  -o <merged.vcook>       Output manifest
  --keywords-file <vkw>   Force keywords_file=... in output (otherwise only kept if all inputs match)
  --verbose               Print merge summary

Examples:
  vshaderc compile -i shaders/pbr.frag.vshader -o out/pbr.frag.vshbin -S frag -I shaders/include -D USE_FOO=1
  vshaderc cook -m examples/keywords/shader_cook.vcook -o out/shaders.vshlib --keywords-file examples/keywords/engine_keywords.vkw --verbose
  vshaderc packlib -o out/shaders.vshlib --keywords-file engine_keywords.vkw out/*.vshbin
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

// ============================================================
// packlib
// ============================================================

static int cmd_packlib(int argc, char** argv)
{
    // vshaderc packlib -o out/shaders.vshlib [--keywords-file path.vkw] <in1.vshbin> <in2.vshbin> ...
    std::string              outPath;
    std::string              keywordsPath;
    std::vector<std::string> inputs;
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
        auto r = read_vshbin_file(path);
        if (!r.isOk())
        {
            log_error("packlib: failed to read " + path + ": " + r.error().message);
            return 4;
        }

        const auto&        bin = r.value();
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

struct CookVariant
{
    std::vector<Define> defines; // additional -D for this variant
};

struct CookShaderJob
{
    std::string              input;
    ShaderStage              stage {ShaderStage::eFrag};
    std::vector<std::string> includeDirs;
    std::string              outDir; // optional (not required for vshlib pipeline)
    std::vector<CookVariant> variants;
};

struct CookManifest
{
    std::vector<CookShaderJob> jobs;
    std::string                keywordsFile; // optional default
};

static Result<CookManifest> load_cook_manifest(const std::string& path)
{
    std::string text;
    if (!read_text_file(path, text))
        return Result<CookManifest>::err({ErrorCode::eIO, "Failed to read manifest: " + path});

    CookManifest  m;
    CookShaderJob cur;
    bool          inShader = false;

    std::istringstream iss(text);
    std::string        line;
    int                lineNo = 0;

    auto flush = [&]() {
        if (inShader)
        {
            if (cur.input.empty())
                return Result<void>::err({ErrorCode::eParseError, "manifest: missing 'input' in [shader]"});
            if (cur.variants.empty())
                cur.variants.push_back({}); // default: one variant, no defines
            m.jobs.push_back(std::move(cur));
            cur      = CookShaderJob {};
            inShader = false;
        }
        return Result<void>::ok();
    };

    while (std::getline(iss, line))
    {
        ++lineNo;
        line = trim_copy(line);
        if (line.empty() || line[0] == '#')
            continue;

        if (line == "[shader]")
        {
            auto fr = flush();
            if (!fr.isOk())
                return Result<CookManifest>::err(fr.error());
            inShader = true;
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos)
            return Result<CookManifest>::err(
                {ErrorCode::eParseError, "manifest:" + std::to_string(lineNo) + ": expected key=value"});

        auto key = trim_copy(line.substr(0, eq));
        auto val = trim_copy(line.substr(eq + 1));

        if (!inShader)
        {
            if (key == "keywords_file" || key == "keywords" || key == "keywords-file")
                m.keywordsFile = normalize_path_slashes(val);
            else
                return Result<CookManifest>::err(
                    {ErrorCode::eParseError,
                     "manifest:" + std::to_string(lineNo) + ": key only valid inside [shader]: " + key});
            continue;
        }

        if (key == "input")
        {
            cur.input = normalize_path_slashes(val);
        }
        else if (key == "stage")
        {
            ShaderStage st;
            if (!parse_stage(val, st))
                return Result<CookManifest>::err(
                    {ErrorCode::eParseError, "manifest:" + std::to_string(lineNo) + ": invalid stage: " + val});
            cur.stage = st;
        }
        else if (key == "includes" || key == "include_dirs" || key == "include")
        {
            split_list(val, cur.includeDirs);
            for (auto& d : cur.includeDirs)
                d = normalize_path_slashes(d);
        }
        else if (key == "outdir" || key == "out_dir")
        {
            cur.outDir = normalize_path_slashes(val);
        }
        else if (key == "variant")
        {
            CookVariant v;
            // IMPORTANT: parse NAME=VALUE pairs correctly
            parse_defines_kv_list(val, v.defines);
            cur.variants.push_back(std::move(v));
        }
        else
        {
            return Result<CookManifest>::err(
                {ErrorCode::eParseError, "manifest:" + std::to_string(lineNo) + ": unknown key: " + key});
        }
    }

    auto fr = flush();
    if (!fr.isOk())
        return Result<CookManifest>::err(fr.error());

    if (m.jobs.empty())
        return Result<CookManifest>::err({ErrorCode::eParseError, "manifest: no [shader] blocks found"});

    return Result<CookManifest>::ok(std::move(m));
}

static Result<void> write_cook_manifest(const std::string& path, const CookManifest& m)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return Result<void>::err({ErrorCode::eIO, "Failed to write manifest: " + path});

    if (!m.keywordsFile.empty())
        f << "keywords_file=" << m.keywordsFile << "\n\n";

    auto stage_to_str = [](ShaderStage st) -> const char* {
        switch (st)
        {
            case ShaderStage::eVert:
                return "vert";
            case ShaderStage::eFrag:
                return "frag";
            case ShaderStage::eComp:
                return "comp";
            case ShaderStage::eTask:
                return "task";
            case ShaderStage::eMesh:
                return "mesh";
            case ShaderStage::eRgen:
                return "rgen";
            case ShaderStage::eRmiss:
                return "rmiss";
            case ShaderStage::eRchit:
                return "rchit";
            case ShaderStage::eRahit:
                return "rahit";
            case ShaderStage::eRint:
                return "rint";
            default:
                return "frag";
        }
    };

    for (const auto& job : m.jobs)
    {
        f << "[shader]\n";
        f << "input=" << job.input << "\n";
        f << "stage=" << stage_to_str(job.stage) << "\n";

        if (!job.includeDirs.empty())
        {
            f << "includes=";
            for (size_t i = 0; i < job.includeDirs.size(); ++i)
            {
                if (i)
                    f << ';';
                f << job.includeDirs[i];
            }
            f << "\n";
        }

        if (!job.outDir.empty())
            f << "outdir=" << job.outDir << "\n";

        for (const auto& v : job.variants)
        {
            std::vector<std::string> lines;
            lines.reserve(v.defines.size());
            for (const auto& d : v.defines)
                lines.push_back(d.value.empty() ? d.name : (d.name + "=" + d.value));
            std::sort(lines.begin(), lines.end());

            f << "variant=";
            for (size_t i = 0; i < lines.size(); ++i)
            {
                if (i)
                    f << ';';
                f << lines[i];
            }
            f << "\n";
        }
        f << "\n";
    }

    return Result<void>::ok();
}

// ============================================================
// cook-merge
// ============================================================

static int cmd_cook_merge(int argc, char** argv)
{
    // vshaderc cook-merge -o merged.vcook <a.vcook> <b.vcook> ... [--keywords-file path.vkw] [--verbose]
    std::string              outPath;
    std::string              keywordsPath;
    bool                     verbose = false;
    std::vector<std::string> inputs;

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
                    log_error("cook-merge: --keywords-file requires a path");
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
        else if (!a.empty() && a[0] == '-')
        {
            log_error("cook-merge: unknown arg: " + a);
            return 2;
        }
        else
        {
            inputs.push_back(a);
        }
    }

    g_verbose = verbose;

    if (outPath.empty() || inputs.empty())
    {
        log_error("cook-merge: -o <merged.vcook> and input manifests are required");
        return 2;
    }

    CookManifest merged;

    // keywords_file resolution (keep only if identical across all inputs unless overridden)
    std::string commonKw;
    bool        commonKwInit = false;

    struct JobKey
    {
        std::string input;
        ShaderStage stage;
    };
    struct JobKeyHash
    {
        size_t operator()(JobKey const& k) const noexcept
        {
            return std::hash<std::string> {}(k.input) ^ static_cast<size_t>(static_cast<uint8_t>(k.stage) << 1);
        }
    };
    struct JobKeyEq
    {
        bool operator()(JobKey const& a, JobKey const& b) const noexcept
        {
            return a.stage == b.stage && a.input == b.input;
        }
    };

    std::unordered_map<JobKey, CookShaderJob, JobKeyHash, JobKeyEq> jobMap;
    jobMap.reserve(inputs.size() * 8);

    for (const auto& in : inputs)
    {
        auto mr = load_cook_manifest(in);
        if (!mr.isOk())
        {
            log_error("cook-merge: failed to load " + in + ": " + mr.error().message);
            return 3;
        }
        auto m = std::move(mr.value());

        auto kw = normalize_path_slashes(m.keywordsFile);
        if (!commonKwInit)
        {
            commonKw     = kw;
            commonKwInit = true;
        }
        else if (kw != commonKw)
        {
            commonKw.clear();
        }

        for (auto& j : m.jobs)
        {
            JobKey key {normalize_path_slashes(j.input), j.stage};

            for (auto& d : j.includeDirs)
                d = normalize_path_slashes(d);
            std::sort(j.includeDirs.begin(), j.includeDirs.end());
            j.includeDirs.erase(std::unique(j.includeDirs.begin(), j.includeDirs.end()), j.includeDirs.end());
            j.outDir = normalize_path_slashes(j.outDir);

            auto it = jobMap.find(key);
            if (it == jobMap.end())
            {
                j.input = key.input;
                jobMap.emplace(key, std::move(j));
            }
            else
            {
                auto& dst = it->second;

                // include dirs
                dst.includeDirs.insert(dst.includeDirs.end(), j.includeDirs.begin(), j.includeDirs.end());
                std::sort(dst.includeDirs.begin(), dst.includeDirs.end());
                dst.includeDirs.erase(std::unique(dst.includeDirs.begin(), dst.includeDirs.end()),
                                      dst.includeDirs.end());

                // outDir: keep existing unless empty
                if (dst.outDir.empty())
                    dst.outDir = j.outDir;

                // variants: de-dup by normalized signature
                std::unordered_set<std::string> seen;
                seen.reserve(dst.variants.size() * 2 + j.variants.size() * 2);

                for (const auto& v : dst.variants)
                    seen.insert(normalize_define_set(v.defines));

                for (const auto& v : j.variants)
                {
                    const auto sig = normalize_define_set(v.defines);
                    if (seen.insert(sig).second)
                        dst.variants.push_back(v);
                }
            }
        }
    }

    merged.keywordsFile = !keywordsPath.empty() ? normalize_path_slashes(keywordsPath) : commonKw;

    merged.jobs.reserve(jobMap.size());
    for (auto& it : jobMap)
    {
        auto& v = it.second;
        std::sort(v.variants.begin(), v.variants.end(), [](const CookVariant& a, const CookVariant& b) {
            return normalize_define_set(a.defines) < normalize_define_set(b.defines);
        });
        merged.jobs.push_back(std::move(v));
    }

    std::sort(merged.jobs.begin(), merged.jobs.end(), [](const CookShaderJob& a, const CookShaderJob& b) {
        if (a.input != b.input)
            return a.input < b.input;
        return static_cast<uint8_t>(a.stage) < static_cast<uint8_t>(b.stage);
    });

    auto wr = write_cook_manifest(outPath, merged);
    if (!wr.isOk())
    {
        log_error("cook-merge: failed to write: " + wr.error().message);
        return 4;
    }

    if (verbose)
    {
        size_t outVars = 0;
        for (auto& j : merged.jobs)
            outVars += j.variants.size();
        log_info("cook-merge: OK -> " + outPath + " jobs=" + std::to_string(merged.jobs.size()) +
                 " variants=" + std::to_string(outVars) +
                 (merged.keywordsFile.empty() ? "" : (" keywords_file=" + merged.keywordsFile)));
    }

    return 0;
}

// ============================================================
// compile (single shader)
// ============================================================

static int cmd_compile(int argc, char** argv)
{
    // vshaderc compile -i <input> -o <out.vshbin> -S <stage> [options]
    std::string              inPath;
    std::string              outPath;
    std::string              stageStr;
    std::vector<std::string> includeDirs;
    std::vector<Define>      defines;
    std::string              keywordsFile;
    bool                     enableCache = true;
    std::string              cacheDir    = ".vshader_cache";
    bool                     verbose     = false;

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
        else if (a == "--cache" && i + 1 < argc)
        {
            cacheDir = argv[++i];
        }
        else if (a == "--verbose")
        {
            verbose = true;
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

    req.hasEngineKeywords = hasEngineKw;
    if (hasEngineKw)
        req.engineKeywords = std::move(engineKw);

    req.enableCache = enableCache;
    req.cacheDir    = cacheDir;

    auto start = std::chrono::steady_clock::now();
    auto r     = build_shader(req);
    auto end   = std::chrono::steady_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    log_info("compile: build_shader took " + std::to_string(ms) + " ms");

    if (!r.isOk())
    {
        log_error("compile: build failed: " + r.error().message);
        return 6;
    }

    auto w = write_vshbin_file(outPath, r.value().binary);
    if (!w.isOk())
    {
        log_error("compile: write failed: " + w.error().message);
        return 7;
    }

    log_info("compile: OK wrote " + outPath + (r.value().fromCache ? " (cache)" : ""));
    if (g_verbose && !r.value().log.empty())
        log_verbose("compile log:\n" + r.value().log);

    return 0;
}

// ============================================================
// cook (single-thread, deterministic)
// ============================================================

static int cmd_cook(int argc, char** argv)
{
    // vshaderc cook -m manifest.vcook -o out/shaders.vshlib [--keywords-file engine_keywords.vkw] [-I dir] [--cache
    // dir]
    // [--no-cache] [-j N|--jobs N] [--skip-invalid] [--verbose]
    std::string              manifestPath;
    std::string              outLibPath;
    std::string              keywordsPath;
    std::vector<std::string> extraIncludeDirs;
    bool                     enableCache = true;
    std::string              cacheDir    = ".vshader_cache";
    int                      jobs        = 0;
    bool                     skipInvalid = false;
    bool                     verbose     = false;

    for (int i = 2; i < argc; ++i)
    {
        std::string a = argv[i];
        if ((a == "-m" || a == "--manifest") && i + 1 < argc)
        {
            manifestPath = argv[++i];
        }
        else if (a == "-o" && i + 1 < argc)
        {
            outLibPath = argv[++i];
        }
        else if (a == "-I" && i + 1 < argc)
        {
            extraIncludeDirs.push_back(normalize_path_slashes(argv[++i]));
        }
        else if ((a == "-j" || a == "--jobs") && i + 1 < argc)
        {
            char*    endptr = nullptr;
            uint64_t val    = std::strtol(argv[++i], &endptr, 10);
            if (*endptr != '\0' || val < 1)
                val = 1;
            jobs = std::max(1, static_cast<int>(val));
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
        else if (a == "-h" || a == "--help")
        {
            print_usage();
            return 0;
        }
        else
        {
            log_error("Unknown cook arg: " + a);
            return 2;
        }
    }

    g_verbose = verbose;

    if (manifestPath.empty() || outLibPath.empty())
    {
        log_error("cook: -m <manifest.vcook> and -o <output.vshlib> are required");
        return 2;
    }

    // Production safety: forced single-thread.
    if (jobs != 1 && verbose)
        log_info("cook: forcing single-thread execution (jobs=1)");
    jobs = 1;

    log_info("cook: loading manifest: " + manifestPath);
    auto mr = load_cook_manifest(manifestPath);
    if (!mr.isOk())
    {
        log_error("cook: failed to load manifest: " + mr.error().message);
        return 3;
    }

    const auto manifestFullPath = std::filesystem::absolute(manifestPath);
    const auto manifestDir      = manifestFullPath.parent_path();

    CookManifest manifest = std::move(mr.value());
    log_info("cook: jobs=" + std::to_string(manifest.jobs.size()));

    if (keywordsPath.empty())
        keywordsPath = (manifestDir / (manifest.keywordsFile)).generic_string();

    EngineKeywordsFile   engineKw;
    bool                 hasEngineKw = false;
    std::vector<uint8_t> keywordsBytes;
    if (!keywordsPath.empty())
    {
        log_info("cook: loading engine keywords: " + keywordsPath);
        auto kwr = load_engine_keywords_vkw(keywordsPath);
        if (!kwr.isOk())
        {
            log_error("cook: failed to parse keywords file: " + kwr.error().message);
            return 3;
        }
        engineKw    = std::move(kwr.value());
        hasEngineKw = true;

        if (!read_binary_file(keywordsPath, keywordsBytes))
        {
            log_error("cook: failed to read keywords bytes: " + keywordsPath);
            return 3;
        }
    }

    // Pre-load shader sources + metadata (metadata needed for only_if + injection discovery)
    struct SrcInfo
    {
        std::string    src;
        ParsedMetadata md;
        bool           hasMd = false;
    };
    std::unordered_map<std::string, SrcInfo> srcCache;
    srcCache.reserve(manifest.jobs.size() * 2);

    for (const auto& job : manifest.jobs)
    {
        auto it = srcCache.find(job.input);
        if (it != srcCache.end())
            continue;

        SrcInfo info;
        auto    inputPath = (manifestDir / job.input).generic_string();
        if (!read_text_file(inputPath, info.src))
        {
            log_error("cook: failed to read shader input: " + job.input);
            return 4;
        }

        auto mdr = parse_vultra_metadata(info.src);
        if (!mdr.isOk())
        {
            log_error("cook: failed to parse shader metadata: " + job.input + ": " + mdr.error().message);
            return 4;
        }
        info.md    = std::move(mdr.value());
        info.hasMd = true;

        srcCache.emplace(job.input, std::move(info));
    }

    // Flatten tasks
    struct Task
    {
        const CookShaderJob* job;
        const CookVariant*   var;
    };

    std::vector<Task> tasks;
    tasks.reserve(512);
    for (const auto& job : manifest.jobs)
        for (const auto& v : job.variants)
            tasks.push_back(Task {&job, &v});

    log_info("cook: total variants=" + std::to_string(tasks.size()));

    std::vector<ShaderLibraryEntry> entries;
    entries.reserve(tasks.size());

    std::unordered_set<uint64_t> seen;
    seen.reserve(tasks.size() * 2);

    size_t      pruned     = 0;
    std::string firstError = {};

    size_t taskIndex = 0;

    for (auto& task : tasks)
    {
        ++taskIndex;

        const auto* job = task.job;
        const auto* var = task.var;

        log_info("cook: variant " + std::to_string(taskIndex) + "/" + std::to_string(tasks.size()) +
                 " shader=" + job->input);

        const auto sit = srcCache.find(job->input);
        if (sit == srcCache.end())
        {
            firstError = "cook: internal error: missing cached source for " + job->input;
            break;
        }

        const SrcInfo& info = sit->second;

        // Start with variant defines
        std::vector<Define> defines = var->defines;

        // Inject engine global permute values for missing permutation keywords (declared in shader metadata)
        if (hasEngineKw && info.hasMd)
        {
            std::unordered_map<std::string, std::string> defMap;
            defMap.reserve(defines.size() * 2);
            for (const auto& d : defines)
                defMap[d.name] = d.value;

            for (const auto& kd : info.md.keywords)
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
                    log_verbose("cook: injected global permute " + kd.name + "=" + iv->second);
                }
            }
        }

        // Constraint pruning (only_if)
        bool skipThisVariant = false;

        if (info.hasMd)
        {
            KeywordValueContext ctx;
            ctx.values.reserve(info.md.keywords.size() * 2);
            ctx.decls.reserve(info.md.keywords.size() * 2);

            for (const auto& kd : info.md.keywords)
                ctx.decls[kd.name] = &kd;

            std::unordered_map<std::string, std::string> defMap;
            defMap.reserve(defines.size() * 2);
            for (const auto& d : defines)
                defMap[d.name] = d.value;

            // resolve values for all keywords: defaults -> defines -> engineKw (global only if not defined)
            for (const auto& kd : info.md.keywords)
            {
                uint32_t v = kd.defaultValue;

                auto it = defMap.find(kd.name);
                if (it != defMap.end())
                {
                    auto pv = parse_keyword_value_local(kd, it->second);
                    if (!pv.isOk())
                    {
                        firstError = "cook: invalid keyword value for " + kd.name + " in " + job->input + ": " +
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
                                "cook: invalid engine keyword value for " + kd.name + ": " + pv.error().message;
                            break;
                        }
                        v = pv.value();
                    }
                }

                ctx.values[kd.name] = v;
            }

            if (!firstError.empty())
                break;

            for (const auto& kd : info.md.keywords)
            {
                if (kd.constraint.empty())
                    continue;

                auto er = eval_only_if(kd.constraint, ctx);
                if (!er.isOk())
                {
                    firstError = "cook: failed to eval only_if for keyword '" + kd.name + "' in " + job->input + ": " +
                                 er.error().message;
                    break;
                }
                if (!er.value())
                {
                    ++pruned;
                    log_verbose("cook: prune by only_if keyword=" + kd.name + " constraint=" + kd.constraint);

                    if (!skipInvalid)
                    {
                        firstError = "cook: variant violates only_if constraint: " + job->input + " (" + kd.name + ")";
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

        // Build request
        BuildRequest req;
        req.source.virtualPath  = job->input;
        req.source.sourceText   = info.src;
        req.options.stage       = job->stage;
        req.options.includeDirs = job->includeDirs;
        for (const auto& idir : extraIncludeDirs)
            req.options.includeDirs.push_back(idir);
        req.options.defines = std::move(defines);

        req.hasEngineKeywords = hasEngineKw;
        if (hasEngineKw)
            req.engineKeywords = engineKw;

        req.enableCache = enableCache;
        req.cacheDir    = cacheDir;

        log_verbose("cook: calling build_shader...");
        auto start = std::chrono::steady_clock::now();
        auto br    = build_shader(req);
        auto end   = std::chrono::steady_clock::now();
        log_verbose("cook: build_shader returned.");

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        log_info("cook: build_shader took " + std::to_string(ms) + " ms");

        if (!br.isOk())
        {
            firstError = "cook: build failed for " + job->input + ": " + br.error().message;
            break;
        }

        const auto& bin = br.value().binary;

        ShaderLibraryEntry e;
        e.keyHash = (bin.variantHash != 0) ? bin.variantHash : bin.contentHash;
        e.stage   = bin.stage;

        log_verbose("processing " + job->input + " shaderIdHash=" + std::to_string(bin.shaderIdHash) + " contentHash=" +
                    std::to_string(bin.contentHash) + " variantHash=" + std::to_string(bin.variantHash) +
                    " keyHash=" + std::to_string(e.keyHash) + " stage=" + std::to_string(static_cast<int>(e.stage)));

        const uint64_t sig =
            xxhash64(&e.keyHash, sizeof(e.keyHash), static_cast<uint64_t>(static_cast<uint8_t>(e.stage)));

        auto bytes = write_vshbin(bin);
        if (!bytes.isOk())
        {
            firstError = "cook: failed to serialize vshbin for " + job->input + ": " + bytes.error().message;
            break;
        }
        e.blob = std::move(bytes.value());

        if (seen.find(sig) != seen.end())
        {
            firstError = "cook: duplicate entry for keyHash=" + std::to_string(e.keyHash) +
                         " stage=" + std::to_string(static_cast<int>(static_cast<uint8_t>(e.stage))) +
                         " (shader: " + job->input + ")";
            break;
        }
        seen.insert(sig);
        entries.push_back(std::move(e));
    }

    if (!firstError.empty())
    {
        log_error(firstError);
        return 5;
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
            log_error("cook: failed to create output directory: " + outDir.generic_string() + ": " + ec.message());
            return 6;
        }
    }
    log_info("cook: writing vshlib: " + outLibPath + " entries=" + std::to_string(entries.size()) +
             " pruned=" + std::to_string(pruned));

    auto w = write_vslib(outLibPath, entries, keywordsBytes.empty() ? nullptr : &keywordsBytes);
    if (!w.isOk())
    {
        log_error("cook: write vshlib failed: " + w.error().message);
        return 7;
    }

    log_info("cook: OK -> " + outLibPath);
    return 0;
}

// ============================================================
// main dispatch
// ============================================================

int main(int argc, char** argv)
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

    if (cmd == "cook")
        return cmd_cook(argc, argv);

    if (cmd == "packlib")
        return cmd_packlib(argc, argv);

    if (cmd == "cook-merge")
        return cmd_cook_merge(argc, argv);

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