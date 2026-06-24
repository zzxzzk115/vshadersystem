#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "test_helpers.hpp"

using namespace vsht;

// Smoke: the simplest possible vertex+fragment shader compiles to SPIR-V and WGSL,
// with correctly named/staged entry points.
TEST_CASE("minimal vertex+fragment compiles to SPIR-V and WGSL")
{
    const char* src = R"SLANG(
        struct VSOut { float4 pos : SV_Position; };
        [shader("vertex")]
        VSOut vertexMain(uint vid : SV_VertexID)
        {
            VSOut o; o.pos = float4(0, 0, 0, 1); return o;
        }
        [shader("fragment")]
        float4 fragmentMain(VSOut i) : SV_Target { return float4(1, 0, 0, 1); }
    )SLANG";

    auto r = compile(src);
    REQUIRE(r.isOk());
    CHECK(r.value().entryPoints.size() == 2);

    const auto* vs = entry(r.value(), ShaderStage::eVert);
    const auto* fs = entry(r.value(), ShaderStage::eFrag);
    REQUIRE(vs);
    REQUIRE(fs);
    CHECK(vs->name == "vertexMain");
    CHECK(fs->name == "fragmentMain");
    CHECK_FALSE(vs->spirv.empty());
    CHECK_FALSE(fs->spirv.empty());
    CHECK_FALSE(vs->wgsl.empty());
    CHECK_FALSE(fs->wgsl.empty());
}

TEST_CASE("a syntactically invalid shader fails with a diagnostic")
{
    const char* src = R"SLANG(
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { this is not valid slang }
    )SLANG";

    auto r = compile(src);
    CHECK_FALSE(r.isOk());
    CHECK_FALSE(r.error().message.empty());
}

TEST_CASE("emitWgsl=false yields SPIR-V only")
{
    const char* src = R"SLANG(
        [shader("fragment")]
        float4 fragmentMain() : SV_Target { return (float4)0; }
    )SLANG";

    auto r = compile(src, {}, opts(/*wgsl=*/false));
    REQUIRE(r.isOk());
    const auto* fs = entry(r.value(), ShaderStage::eFrag);
    REQUIRE(fs);
    CHECK_FALSE(fs->spirv.empty());
    CHECK(fs->wgsl.empty());
}
