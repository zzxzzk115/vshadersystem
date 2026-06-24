#include <doctest/doctest.h>

#include "test_helpers.hpp"

#include <vshadersystem/vsh_format.hpp>

using namespace vsht;

TEST_CASE("import a shared module from the in-memory VFS")
{
    std::vector<vshaderc::SlangSourceFile> lib = {
        {"brdf.slang", "module brdf;\npublic float3 lambert(float3 albedo) { return albedo * 0.318; }"},
    };

    const char* src = R"SLANG(
        import brdf;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return float4(lambert((float3)1), 1); }
    )SLANG";

    auto r = compile(src, lib);
    REQUIRE(r.isOk());
    CHECK(entry(r.value(), ShaderStage::eFrag));

    // the imported module is recorded as a dependency (for cache invalidation)
    bool sawBrdf = false;
    for (const auto& d : r.value().dependencies)
        if (d.find("brdf") != std::string::npos)
            sawBrdf = true;
    CHECK(sawBrdf);
}

TEST_CASE("a missing import fails cleanly")
{
    const char* src = R"SLANG(
        import does_not_exist;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return (float4)1; }
    )SLANG";

    auto r = compile(src);
    CHECK_FALSE(r.isOk());
}

TEST_CASE("nested imports resolve (module imports another module)")
{
    std::vector<vshaderc::SlangSourceFile> lib = {
        {"math.slang", "module math;\npublic float sqr(float x) { return x * x; }"},
        {"shading.slang", "module shading;\nimport math;\npublic float falloff(float d) { return 1.0 / sqr(d); }"},
    };

    const char* src = R"SLANG(
        import shading;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return (float4)falloff(2.0); }
    )SLANG";

    auto r = compile(src, lib);
    REQUIRE(r.isOk());
    CHECK(entry(r.value(), ShaderStage::eFrag));
}

TEST_CASE("a packed .vshslang library feeds the compiler VFS")
{
    namespace v1 = vshadersystem::v1;

    // pack a library, then unpack it into VFS source files for compilation
    std::vector<v1::SourceFile> pack = {
        {"tone.slang", "module tone;\npublic float3 reinhard(float3 c) { return c / (c + 1.0); }"},
    };
    auto enc = v1::write_source_pack(pack);
    REQUIRE(enc.isOk());
    auto dec = v1::read_source_pack(enc.value());
    REQUIRE(dec.isOk());

    std::vector<vshaderc::SlangSourceFile> vfs;
    for (const auto& f : dec.value())
        vfs.push_back({f.path, f.text});

    const char* src = R"SLANG(
        import tone;
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return float4(reinhard((float3)2), 1); }
    )SLANG";

    auto r = compile(src, vfs);
    REQUIRE(r.isOk());
    CHECK(entry(r.value(), ShaderStage::eFrag));
}
