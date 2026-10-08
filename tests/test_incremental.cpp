#include <doctest/doctest.h>

#include "cook_cache.hpp"
#include "test_helpers.hpp"
#include <vshaderc/cli.hpp>
#include <vshaderc/slang_build.hpp>
#include <vshadersystem/vsh_format.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#include <io.h>
#define test_dup _dup
#define test_dup2 _dup2
#define test_close _close
#define test_fileno _fileno
#else
#include <unistd.h>
#define test_dup dup
#define test_dup2 dup2
#define test_close close
#define test_fileno fileno
#endif

namespace
{
    namespace fs       = std::filesystem;
    const char* shader = R"(
import vsh;
[[vk::binding(0, 0)]] RWStructuredBuffer<float> outputValues;
[shader("compute")] [numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) { outputValues[tid.x] = 1; }
)";
    struct Fixture
    {
        fs::path dir =
            fs::temp_directory_path() /
            ("vshaderc-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Fixture() { fs::create_directories(dir / "shaders"); }
        ~Fixture()
        {
            std::error_code ec;
            // Delete only the uniquely-owned temporary fixture, never a computed parent.
            if (fs::weakly_canonical(dir).parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
                dir.filename().string().starts_with("vshaderc-cache-test-"))
                fs::remove_all(dir, ec);
        }
        void write(const fs::path& relative, std::string_view text)
        {
            fs::create_directories((dir / relative).parent_path());
            std::ofstream file(dir / relative, std::ios::binary);
            file << text;
        }
        std::vector<uint8_t> bytes() const
        {
            std::ifstream file(dir / "out.vshlib", std::ios::binary);
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }
        std::pair<int, std::string> run(std::vector<std::string> extra = {})
        {
            std::vector<std::string> args {"vshaderc",
                                           "build",
                                           "--shader_root",
                                           (dir / "shaders").string(),
                                           "-o",
                                           (dir / "out.vshlib").string(),
                                           "--no-wgsl",
                                           "--quiet"};
            args.insert(args.end(), extra.begin(), extra.end());
            std::vector<char*> argv;
            for (auto& arg : args)
                argv.push_back(arg.data());
            FILE* capture = std::tmpfile();
            REQUIRE(capture != nullptr);
            std::fflush(stdout);
            const int saved = test_dup(test_fileno(stdout));
            REQUIRE(saved >= 0);
            struct Restore
            {
                int   saved;
                FILE* capture;
                ~Restore()
                {
                    std::fflush(stdout);
                    test_dup2(saved, test_fileno(stdout));
                    test_close(saved);
                    std::fclose(capture);
                }
            } restore {saved, capture};
            REQUIRE(test_dup2(test_fileno(capture), test_fileno(stdout)) >= 0);
            const int code = vshaderc::cli::run(static_cast<int>(argv.size()), argv.data());
            std::fflush(stdout);
            std::rewind(capture);
            std::string output;
            char        buffer[4096];
            while (const auto n = std::fread(buffer, 1, sizeof(buffer), capture))
                output.append(buffer, n);
            return {code, output};
        }
        void check(size_t compiled, size_t cached, std::vector<std::string> extra = {})
        {
            const auto [code, output] = run(std::move(extra));
            INFO(output);
            REQUIRE(code == 0);
            CHECK(output.find("(compiled " + std::to_string(compiled) + ", cached " + std::to_string(cached) + ")") !=
                  std::string::npos);
        }
    };
} // namespace

TEST_CASE("CLI cache skips unchanged shaders and preserves output mtime")
{
    Fixture f;
    f.write("shaders/a.slang", shader);
    f.write("shaders/b.slang", shader);
    f.check(2, 0);
    const auto bytes = f.bytes();
    const auto stamp = fs::last_write_time(f.dir / "out.vshlib");
    f.check(0, 2);
    CHECK(f.bytes() == bytes);
    CHECK(fs::last_write_time(f.dir / "out.vshlib") == stamp);
    const auto inputStamp = fs::last_write_time(f.dir / "shaders/a.slang");
    f.write("shaders/a.slang", std::string(shader) + "\n// comment-only edit\n");
    fs::last_write_time(f.dir / "shaders/a.slang", inputStamp); // content, not timestamps
    f.check(1, 1);
    CHECK(f.bytes() == bytes);
    f.check(0, 2, {"--jobs", "1"}); // execution parallelism is not a shader cache key
    f.check(2, 0, {"--no-cache"});
    CHECK(f.bytes() == bytes);
    f.check(0, 2);
    f.write("shaders/c.slang", shader);
    f.check(1, 2);
    REQUIRE(vshadersystem::v1::read_library(f.bytes()).value().entries.size() == 3);
    fs::remove(f.dir / "shaders/c.slang");
    f.check(0, 2);
    CHECK(f.bytes() == bytes);
}

TEST_CASE("CLI cache tracks transitive imports and higher-priority absent probes")
{
    Fixture f;
    f.write("shaders/a.slang",
            std::string("import helper;\n") +
                std::string(shader).replace(std::string(shader).find("= 1;"), 4, "= helperValue();"));
    f.write("shaders/b.slang", shader);
    f.write("low/helper.slang", "import leaf; public float helperValue() { return leafValue(); }\n");
    f.write("low/leaf.slang", "public float leafValue() { return 1; }\n");
    fs::create_directory(f.dir / "high");
    const std::vector<std::string> includes {"-I", (f.dir / "high").string(), "-I", (f.dir / "low").string()};
    f.check(2, 0, includes);
    f.check(0, 2, includes);
    const auto old   = f.bytes();
    const auto stamp = fs::last_write_time(f.dir / "low/leaf.slang");
    f.write("low/leaf.slang", "public float leafValue() { return 2; }\n");
    fs::last_write_time(f.dir / "low/leaf.slang", stamp);
    f.check(1, 1, includes);
    CHECK(f.bytes() != old);
    f.write("low/unused.slang", "public float unused() { return 9; }\n");
    f.check(0, 2, includes);
    f.write("high/leaf.slang", "public float leafValue() { return 3; }\n");
    f.check(1, 1, includes);
    const auto good = f.bytes();
    fs::remove(f.dir / "high/leaf.slang");
    fs::remove(f.dir / "low/leaf.slang");
    CHECK(f.run(includes).first == 1);
    CHECK(f.bytes() == good); // failed cook preserves the complete previous library
}

TEST_CASE("CLI cache isolates options and recovers from damaged artifacts")
{
    Fixture f;
    f.write("shaders/a.slang", shader);
    f.check(1, 0);
    const auto plain = f.bytes();
    f.check(1, 0, {"--debug-info"});
    CHECK(f.bytes() != plain);
    f.check(0, 1, {"--debug-info"});
    f.check(1, 0, {"--matrix-layout", "row"});
    f.check(1, 0, {"--optimize"});
    f.check(0, 1);
    CHECK(f.bytes() == plain);
    for (const auto& entry : fs::directory_iterator(f.dir / "out.vshlib.cache"))
        std::ofstream(entry.path(), std::ios::binary | std::ios::trunc) << "corrupted";
    f.check(1, 0);
    CHECK(f.bytes() == plain);
    CHECK(f.run({"--jobs", "0"}).first == 2);
    CHECK(f.run({"--jobs", "garbage"}).first == 2);
}

TEST_CASE("Parallel permutations preserve per-variant reflection and serialized bytes")
{
    const std::string            source = R"(
import vsh;
[VshKeyword("EXTRA", "bool", "permute", "local")]
[VshKeyword("OTHER", "bool", "permute", "local")]
void __vsh_meta() {}
[[vk::binding(0,0)]] RWStructuredBuffer<float> outputValues;
#if EXTRA
[[vk::binding(1,0)]] StructuredBuffer<float> inputValues;
#endif
[shader("compute")] [numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
#if EXTRA
outputValues[tid.x] = inputValues[tid.x];
#else
#if OTHER
outputValues[tid.x] = 2;
#else
outputValues[tid.x] = 1;
#endif
#endif
}
)";
    vshaderc::ShaderBuildOptions options;
    options.shaderId = "parallel";
    auto serial      = vshaderc::build_shader(vsht::compiler(), "parallel", "parallel.slang", source, options);
    INFO(serial.error().message);
    REQUIRE(serial.isOk());
    options.jobs  = 4;
    auto parallel = vshaderc::build_shader(vsht::compiler(), "parallel", "parallel.slang", source, options);
    INFO(parallel.error().message);
    REQUIRE(parallel.isOk());
    REQUIRE(parallel.value().variants.size() == serial.value().variants.size());
    for (size_t i = 0; i < serial.value().variants.size(); ++i)
    {
        const auto a = vshaderc::to_shader_binary(
            serial.value().variants[i], serial.value().shaderIdHash, serial.value().keywords);
        const auto b = vshaderc::to_shader_binary(
            parallel.value().variants[i], parallel.value().shaderIdHash, parallel.value().keywords);
        CHECK(vshadersystem::v1::write_binary(a).value() == vshadersystem::v1::write_binary(b).value());
        CHECK(b.reflection.descriptors.size() == (parallel.value().variants[i].keywordValues[0].second ? 2 : 1));
    }
}

TEST_CASE("Compiler identity participates in cache keys")
{
    Fixture                      f;
    vshaderc::ShaderBuildOptions options;
    vshaderc::detail::CookCache  a(f.dir / "cache", "compiler A");
    vshaderc::detail::CookCache  b(f.dir / "cache", "compiler B");
    CHECK(a.key(f.dir / "main.slang", shader, options, {}) != b.key(f.dir / "main.slang", shader, options, {}));
}

TEST_CASE("Zero permutation cap rejects shaders without keywords")
{
    vshaderc::ShaderBuildOptions options;
    options.shaderId    = "cap";
    options.maxVariants = 0;
    const auto result   = vshaderc::build_shader(vsht::compiler(), "cap", "cap.slang", shader, options);
    REQUIRE_FALSE(result.isOk());
    CHECK(result.error().code == vshadersystem::ErrorCode::eCompileError);
}
